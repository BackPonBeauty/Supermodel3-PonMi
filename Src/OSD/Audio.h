/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011 Bart Trzynadlowski, Nik Henson
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free 
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/
 
/*
 * Audio.h
 * 
 * Header file for OS-dependent audio playback interface.
 */

#ifndef INCLUDED_AUDIO_H
#define INCLUDED_AUDIO_H

#include "Types.h"
#include "Util/NewConfig.h"
#include "Game.h"

typedef void (*AudioCallbackFPtr)(void *data);

extern void SetAudioCallback(AudioCallbackFPtr callback, void *data);

extern void SetAudioEnabled(bool enabled);
extern void SetAudioType(Game::AudioTypes type);

/*
 * OpenAudio()
 *
 * Initializes the audio system.
 */
extern Result OpenAudio(const Util::Config::Node& config);

/*
 * OutputAudio(unsigned numSamples, *INT16 leftBuffer, *INT16 rightBuffer)
 *
 * Sends a chunk of two-channel audio with the given number of samples to the audio system.
 */
extern bool OutputAudio(unsigned numSamples, const float* leftFrontBuffer, const float* rightFrontBuffer, const float* leftRearBuffer, const float* rightRearBuffer, bool flipStereo);

/*
 * CloseAudio()
 *
 * Shuts down the audio system.
 */
extern void CloseAudio();

#endif	// INCLUDED_AUDIO_H
