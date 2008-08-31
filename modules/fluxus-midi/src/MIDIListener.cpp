/*
 Copyright (C) 2008 Gabor Papp

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <iostream>

#include "MIDIListener.h"

using namespace std;

/** maximum number of midi notes stored */
static const unsigned MAX_MIDI_NOTE_COUNT = 256;

MIDINote::MIDINote(int _on_off, int _channel, int _note, int _velocity) :
	on_off(_on_off),
	channel(_channel),
	note(_note),
	velocity(_velocity)
{
}

void midi_callback(double deltatime, vector<unsigned char> *message,
		void *user_data)
{
	static_cast<MIDIListener *>(user_data)->callback(deltatime, message);
}

MIDIListener::MIDIListener(int port /*= -1*/) :
	midiin(NULL),
	last_event("")
{
	init_midi();

	if (port >= 0)
		open(port);

	/* allocate array for controller values and clear it */
	cntrl_values = new unsigned char[MAX_CNTRL];
	fill(cntrl_values, cntrl_values + MAX_CNTRL, 0);

	pthread_mutex_init(&mutex, NULL);
}

MIDIListener::~MIDIListener()
{
	if (midiin)
		delete midiin;

	delete [] cntrl_values;

	deque<MIDINote *>::iterator i = midi_notes.begin();
	for (; i < midi_notes.end(); i++)
	{
		delete *i;
	}
	midi_notes.clear();

	pthread_mutex_destroy(&mutex);
}

void MIDIListener::init_midi(void)
{
	if (midiin == NULL)
	{
		try
		{
			midiin = new RtMidiIn();
		}
		catch (RtError &error)
		{
			error.printMessage();
			midiin = NULL;
		}
		/* ignore MIDI sysex, timing and active sensing messages */
		midiin->ignoreTypes(true, true, true);
	}

	deque<MIDINote *>::iterator i = midi_notes.begin();
	for (; i < midi_notes.end(); i++)
	{
		delete *i;
	}
	midi_notes.clear();
}

/**
 * Returns MIDI input port information.
 * \retval vector<string> string identifiers for the available MIDI
 *			input ports
 **/
vector<string> MIDIListener::info(void)
{
	port_names.clear();
	if (midiin == NULL)
	{
		init_midi();
		if (midiin == NULL)
			return port_names;
	}

	int port_count = midiin->getPortCount();
	std::string port_name;
	for (int i = 0; i < port_count; i++)
	{
		try
		{
			port_name = midiin->getPortName(i);
			port_names.push_back(port_name);
		}
		catch (RtError &error)
		{
			error.printMessage();
			break;
		}
	}

	return port_names;
}

/**
 * Opens MIDI port.
 * \param port port to open
 **/
void MIDIListener::open(int port)
{
	static bool callback_set = false;

	if (midiin == NULL)
	{
		init_midi();
		if (midiin == NULL)
			return;
	}

	int port_count = midiin->getPortCount();
	if (port >= port_count)
	{
		cerr << "midi listener: invalid port\n";
		return;
	}

	try
	{
		midiin->openPort(port);
	}
	catch (RtError &error)
	{
		error.printMessage();
		return;
	}

	if (callback_set)
	{
		midiin->cancelCallback();
	}
	midiin->setCallback(&midi_callback, this);
	callback_set = true;
}

/**
 * Returns controller values.
 * \param channel MIDI channel
 * \param cntrl_number controller number
 * \retval int controller value
 **/
int MIDIListener::get_cc(int channel, int cntrl_number)
{
	if (midiin == NULL)
	{
		init_midi();
		if (midiin == NULL)
			return 0;
	}

	pthread_mutex_lock(&mutex);
	int v = cntrl_values[(channel << 7) + cntrl_number];
	pthread_mutex_unlock(&mutex);
	return v;
}

/**
 * Returns normalised controller values.
 * \param channel MIDI channel
 * \param cntrl_number controller number
 * \retval float controller value normalised to the [0, 1] interval
 **/
float MIDIListener::get_ccn(int channel, int cntrl_number)
{
	if (midiin == NULL)
	{
		init_midi();
		if (midiin == NULL)
			return 0;
	}

	pthread_mutex_lock(&mutex);
	float v = (float)cntrl_values[(channel << 7) + cntrl_number] / 127.0;
	pthread_mutex_unlock(&mutex);
	return v;
}

/**
 * Returns last MIDI event as string.
 * \retval string MIDI event in string format
 **/
string MIDIListener::get_last_event(void)
{
	static string last_event_static = "";

	pthread_mutex_lock(&mutex);
	last_event_static = last_event;
	pthread_mutex_unlock(&mutex);

	return last_event_static;
}

/**
 * Returns next MIDI note event from event queue.
 * \retval MIDINote* pointer to MIDI note or NULL if the event queue is empty
 **/
MIDINote *MIDIListener::get_note(void)
{
	static MIDINote note;

	pthread_mutex_lock(&mutex);
	if (midi_notes.empty())
	{
		pthread_mutex_unlock(&mutex);
		return NULL;
	}

	MIDINote *n = midi_notes.front();
	midi_notes.pop_front();
	pthread_mutex_unlock(&mutex);

	note.on_off = n->on_off;
	note.channel = n->channel;
	note.note = n->note;
	note.velocity = n->velocity;
	delete n;

	return &note;
}

/**
 * Adds a new note to the event queue.
 **/
void MIDIListener::add_note(int on_off, int ch, int note, int velocity)
{
	MIDINote *n = new MIDINote(on_off, ch, note, velocity);

	midi_notes.push_back(n);
	while (midi_notes.size() > MAX_MIDI_NOTE_COUNT)
	{
		delete midi_notes.front();
		midi_notes.pop_front();
	}
}

void MIDIListener::callback(double deltatime, vector<unsigned char> *message)
{
	char buf[256];

	unsigned int count = message->size();
	int status = (*message)[0] >> 4;
	int ch = (*message)[0] & 0xf;

	switch (status)
	{
		case MIDIListener::MIDI_CONTROLLER:
			if (count == 3)
			{
				int cntrl_number = (*message)[1]; /* controller number */
				/* array index from channel and controller number */
				int i = (ch << 7) + cntrl_number;
				int value = (*message)[2]; /* controller value */
				pthread_mutex_lock(&mutex);
				cntrl_values[i] = value;
				pthread_mutex_unlock(&mutex);
			}
			break;

		case MIDIListener::MIDI_NOTE_OFF:
		case MIDIListener::MIDI_NOTE_ON:
			if (count == 3)
			{
				pthread_mutex_lock(&mutex);
				add_note(status, ch, (*message)[1], (*message)[2]);
				pthread_mutex_unlock(&mutex);
			}
			break;

		default:
			break;
	}

	/* debug string for last_event */
	for (unsigned i = 0; i < count; i++)
	{
		if (i == 0)
		{
			switch (status)
			{
				case MIDIListener::MIDI_NOTE_OFF:
					snprintf(buf, 256, "%d (note off) %d ", status, ch);
					last_event = string(buf);
					break;

				case MIDIListener::MIDI_NOTE_ON:
					snprintf(buf, 256, "%d (note on) %d ", status, ch);
					last_event = string(buf);
					break;

				case MIDIListener::MIDI_CONTROLLER:
					snprintf(buf, 256, "%d (cc) %d ", status, ch);
					last_event = string(buf);
					break;

				default:
					snprintf(buf, 256, "%d %d ", status, ch);
					last_event = string(buf);
					break;
			}
		}
		else
		{
			snprintf(buf, 256, "%d ", (*message)[i]);
			last_event += string(buf);
		}
	}
}
