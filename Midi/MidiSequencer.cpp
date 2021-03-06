#include "MidiSequencer.h"

MidiSequencer::MidiSequencer(QObject *parent) : QThread(parent)
{
    _midi = new MidiFile();
    _eTimer = new QElapsedTimer();
}

MidiSequencer::~MidiSequencer()
{
    stop();

    delete _eTimer;
    delete _midi;
}

int MidiSequencer::beatCount()
{
    if (_midi->events().count() == 0) {
        return 0;
    } else {
        return _midi->beatFromTick(durationTick());
    }
}

int MidiSequencer::currentBeat()
{
    return _midi->beatFromTick(positionTick());
}

int MidiSequencer::positionTick()
{
    if (_playing) {
        return _midi->tickFromTimeMs(_eTimer->elapsed()  + _startPlayTime, _midiSpeed);
    } else {
        return _positionTick;
    }
}

int MidiSequencer::durationTick()
{
    return _midi->events().back()->tick();
}

long MidiSequencer::positionMs()
{
    return _playing ? _midi->timeFromTick(positionTick()) * 1000 : _positionMs;
}

long MidiSequencer::durationMs()
{
    return _midi->timeFromTick(durationTick()) * 1000;
}

void MidiSequencer::setPositionTick(int t)
{
    bool playAfterSeek = _playing;
    if (_playing)
        stop();

    uint32_t tick = 0;
    if (t < _startTick)
        tick = _startTick;
    else if (t > _endTick)
        tick = _endTick;
    else
        tick = t;

    _mutex.lock();

    _playedIndex = eventIndexFromTick(tick);
    _startPlayTime = _midi->timeFromTick(tick, _midiSpeed) * 1000;
    _positionMs = _startPlayTime;
    _positionTick = t;

    _mutex.unlock();

    if (playAfterSeek)
        start();
}

void MidiSequencer::setBpmSpeed(int sp)
{
    if (sp == _midiSpeed)
        return;

    if ((_midiBpm + sp) < 20 || (_midiBpm + sp) > 250)
        return;

    _mutex.lock();

    if (_playing) {
        _midiSpeedTemp = sp;
        _midiChangeBpmSpeed = true;
    } else {
        _midiSpeed = sp;
    }

    _mutex.unlock();

    emit bpmChanged(_midiBpm + sp);
}

bool MidiSequencer::load(const QString &file, bool seekFileChunkID)
{
    if (!_stopped)
        stop();

    if (!_midi->read(file, seekFileChunkID))
        return false;

    _midiSpeed = 0;
    _midiSpeedTemp = 0;
    _midiChangeBpmSpeed = false;
    _endTick = _midi->events().last()->tick();

    _finished = false;

    if (_midi->tempoEvents().count() > 0) {
        _midiBpm = _midi->tempoEvents()[0]->bpm();
    } else {
        _midiBpm = 120;
    }

    return true;
}

void MidiSequencer::stop(bool resetPos)
{
    if (_stopped)
        return;

    _mutex.lock();
    _playing = false;
    _stopped = false;
    _startPlayIndex = _playedIndex;
    _startPlayTime = positionMs();

    _waitCondition.wakeAll();
    _mutex.unlock();

    wait();

    if (resetPos) {
        _mutex.lock();
        _stopped = true;
        _startPlayTime = _midi->timeFromTick(_startTick, _midiSpeed) * 1000;;
        _startPlayIndex = eventIndexFromTick(_startTick);
        _playedIndex = _startPlayIndex;
        _positionMs = _startPlayTime;
        _positionTick = _startTick;
        _mutex.unlock();
    }
}

void MidiSequencer::setStartTick(int tick)
{
    _startTick = tick;

    if (_stopped) {
        _mutex.lock();
        _startPlayTime = _midi->timeFromTick(tick, _midiSpeed) * 1000;
        _startPlayIndex = eventIndexFromTick(tick);
        _playedIndex = _startPlayIndex;
        _positionMs = _startPlayTime;
        _positionTick = tick;
        _mutex.unlock();
    }
}

void MidiSequencer::run()
{
    if (_playing)
        return;

    _mutex.lock();

    _playing = true;
    _stopped = false;
    _finished = false;

    _mutex.unlock();

    if (_positionTick <= _startTick) {
        for (MidiEvent *e : _midi->controllerAndProgramEvents()) {
            if (e->tick() > _startTick)
                break;
            emit playingEvent(e);
        }
    }

    _eTimer->restart();

    for (int i = _playedIndex; i < _midi->events().count(); i++) {

        if (!_playing)
            break;

        _mutex.lock();

        if (_midi->events()[i]->eventType() != MidiEventType::Meta) {

            uint32_t tick = _midi->events()[i]->tick();

            if (_midiChangeBpmSpeed) {
                _midiChangeBpmSpeed = false;
                _midiSpeed = _midiSpeedTemp;
                _startPlayTime = _midi->timeFromTick(_midi->events()[i-1]->tick(), _midiSpeed) * 1000;
                _eTimer->restart();
            }

            long eventTime = (_midi->timeFromTick(tick, _midiSpeed)  * 1000);
            long waitTime = eventTime - _startPlayTime  - _eTimer->elapsed();

            if (waitTime > 0) {
                //msleep(waitTime);
                _waitCondition.wait(&_mutex, waitTime);
            }

            _positionMs = eventTime;


        } else { // Meta event
            if (_midi->events()[i]->metaEventType() == MidiMetaType::SetTempo) {
                _midiBpm = _midi->events()[i]->bpm();
                emit bpmChanged(_midiBpm + _midiSpeed);
            }
        }

        emit playingEvent(_midi->events()[i]);

        _playedIndex = i;
        _positionTick = _midi->events()[i]->tick();

        _mutex.unlock();

    } // End for loop

    if (_playedIndex == _midi->events().size() -1 ) {
        _mutex.lock();
        _finished = true;
        _mutex.unlock();
    }
}

int MidiSequencer::eventIndexFromTick(int tick)
{
    int index = 0;
    for (MidiEvent *e :_midi->events()) {
        if (e->tick() >= tick)
            break;

        index++;
    }

    return index;
}
