/* 
 *  Karplus-Strong synthesis example
 *  
 *  Controls:
 *   Input 1: trigger
 *   Input 2: Pitch
 *   Input 3: Stimulation type (choice of noise, saw or square - mostly noise though)
 *
 * Copyright (C) 2021  John A. Tuffen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Questions/queries can be directed to info@wonkystuff.net
 */

#include "calc.h"

// Base-timer is running at 16MHz
#define F_TIM (16000000L)

// Fixed value to start the ADC
// enable ADC, start conversion, prescaler = /64 gives us an ADC clock of 8MHz/64 (125kHz)
#define ADCSRAVAL ( _BV(ADEN) | _BV(ADSC) | _BV(ADPS2) | _BV(ADPS1)  | _BV(ADIE) )

// let the preprocessor calculate the various register values 'coz
// they don't change after compile time
#if ((F_TIM/(SRATE)) < 255)
#define T1_MATCH ((F_TIM/(SRATE))-1)
#define T1_PRESCALE _BV(CS00)  //prescaler clk/1 (i.e. 8MHz)
#else
#define T1_MATCH (((F_TIM/8L)/(SRATE))-1)
#define T1_PRESCALE _BV(CS01)  //prescaler clk/8 (i.e. 1MHz)
#endif


#define OSCOUTREG (OCR2A)     // Grains output port

#define WTSIZE  256u

uint8_t waveTable[WTSIZE];

typedef struct {
  uint16_t phase;
  uint16_t phase_inc;
} accumulator_t;


void setup()
{
  CLKPR = _BV(CLKPCE);
  CLKPR = 0;
  
  ///////////////////////////////////////////////
  // Set up Timer/Counter1 for 250kHz PWM output
  TCCR2A = 0;                  // stop the timer
  TCCR2B = 0;                  // stop the timer
  TCNT2 = 0;                   // zero the timer
  GTCCR = _BV(PSRASY);         // reset the prescaler
  TCCR2A = _BV(WGM20)  | _BV(WGM21) |  // fast PWM to OCRA
           _BV(COM2A1) | _BV(COM2A0);  // OCR2A set at match; cleared at start
  TCCR2B = _BV(CS20);                  // fast pwm part 2; no prescale on input clock
  //OSCOUTREG = 128;                   // start with 50% duty cycle on the PWM
  pinMode(11, OUTPUT);                 // PWM output pin (grains)

  ///////////////////////////////////////////////
  // Set up Timer/Counter0 for sample-rate ISR
  TCCR0B = 0;                 // stop the timer (no clock source)
  TCNT0 = 0;                  // zero the timer

  TCCR0A = _BV(WGM01);        // CTC Mode
  TCCR0B = T1_PRESCALE;
  OCR0A  = T1_MATCH;          // calculated match value
  TIMSK0 |= _BV(OCIE0A);

  pinMode(8, OUTPUT);         // marker
}

/*
 * Simple function to give us an 8 bit
 * psuedo-random number sequence.
 * Algorithm from http://doitwireless.com/2014/06/26/8-bit-pseudo-random-number-generator/
 */

uint8_t
wsRnd8(void)
{
  static uint8_t r = 0x23;
  uint8_t lsb = r & 1;
  r >>= 1;
  r ^= (-lsb) & 0xB8;
  return r;
}

// There are no real time constraints here, this is an idle loop after
// all...

volatile accumulator_t accum;

void loop()
{
  uint8_t stimType = analogRead(0) >> 7;
  uint8_t stimAmp = analogRead(2) >> 7; // gives us 0-7 from 3 bits.
  static uint8_t lastStimAmp;

  accum.phase_inc = pgm_read_word(&octaveLookup[analogRead(1)]);

  if ((stimAmp > 3) &&
      (lastStimAmp < stimAmp))
  {
    int i;
    uint8_t stimShift = 7 - stimAmp;  // 7-0
    for(i=0; i<WTSIZE;i++)
    {
      uint8_t v=0;
      switch (stimType)
      {
        case 4:
          v = i;             // ramp
          break;
        case 7:
          v = (i<128) ? 0 : 255;  // square
          break;
        default:
          v = wsRnd8();  // noise
          break;
      }
      waveTable[i] = v >> stimShift;

    }
  }
  lastStimAmp = stimAmp;
}

// deal with oscillator
ISR(TIMER0_COMPA_vect)
{
  uint8_t outVal = 0;
  static uint8_t lastOut=0x80;

  uint8_t p = (accum.phase + 128) >> 8; // Wavetable is 256 entries, so shift the
                                        // 16bit phase-accumulator value to leave us with 8bits
                                        // with which to index into the table
                                        // (adding 128 (i.e. 0.5) to the phase-accumulator here rounds the
                                        // actual phase to the nearest integer. Sounds better
                                        // than simple truncation)
  outVal = waveTable[p];                // read the wavetable value at the current phase position
  waveTable[p] = (lastOut+outVal)/2;    // write back to the wavetable the average of the last
                                        // output and this output
  lastOut = outVal;
  
  accum.phase += accum.phase_inc;       // move the oscillator's phase-accumulator on

  // invert the wave for the second half
  OSCOUTREG = outVal;
}
