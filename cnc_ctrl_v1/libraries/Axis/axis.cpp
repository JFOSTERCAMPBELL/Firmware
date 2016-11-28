/*This file is part of the Makesmith Control Software.

    The Makesmith Control Software is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Makesmith Control Software is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with the Makesmith Control Software.  If not, see <http://www.gnu.org/licenses/>.
    
    Copyright 2014-2016 Bar Smith*/


#include "Arduino.h"
#include "Axis.h"

#define FORWARD 1
#define BACKWARD -1

#define EEPROMVALIDDATA 56
#define EEPROMFLAG 18

//13968 is correct
#define NUMBER_OF_ENCODER_STEPS 8148.0 



Axis::Axis(int pwmPin, int directionPin1, int directionPin2, int encoderPin1, int encoderPin2, String axisName, int eepromAdr, float mmPerRotation)
:
_encoder(encoderPin1,encoderPin2)
{
    
    //initialize motor
    _motor.setupMotor(pwmPin, directionPin1, directionPin2);
    
    _pidController.setup(&_pidInput, &_pidOutput, &_pidSetpoint, _Kp, _KiFar, _Kd, REVERSE);
    
    //initialize variables
    _direction    = FORWARD;
    _axisName     = axisName;
    _axisTarget   = 0.0;
    _eepromAdr    = eepromAdr;
    _mmPerRotation= mmPerRotation;
    
    //load position
    if (EEPROM.read(EEPROMFLAG) == EEPROMVALIDDATA){
        set(_readFloat(_eepromAdr));
    }
    
    
}

void   Axis::initializePID(){
    _pidController.SetMode(AUTOMATIC);
    _pidController.SetOutputLimits(-90, 90);
}

int    Axis::write(float targetPosition){
    
    _pidSetpoint   =  targetPosition/_mmPerRotation;
    
    int acceptableError = 20;
    if (abs( ((_encoder.read()/NUMBER_OF_ENCODER_STEPS) - _pidSetpoint)*1000 ) < acceptableError){
        return 1;
    }
    else{
        return 0;
    }
}

float  Axis::read(){
    
    if (_motor.attached()){
        return (_encoder.read()/NUMBER_OF_ENCODER_STEPS)*_mmPerRotation;
    }
    else{
        return (_encoder.read()/NUMBER_OF_ENCODER_STEPS)*_mmPerRotation;
    }
}

float  Axis::target(){
    return _axisTarget*_mmPerRotation;
}

float  Axis::setpoint(){
    return _pidSetpoint*_mmPerRotation;
}

int    Axis::set(float newAxisPosition){
    _axisTarget   =  newAxisPosition/_mmPerRotation;
    _encoder.write((newAxisPosition*NUMBER_OF_ENCODER_STEPS)/_mmPerRotation);
}

void   Axis::computePID(){
    
    if (_disableAxisForTesting){
        return;
    }
    
    if (_change(_sign(_oldSetpoint - _pidSetpoint))){ //this determines if the axis has changed direction of movement and flushes the acumulator in the PID if it has
        _pidController.FlipIntegrator();
    }
    _oldSetpoint = _pidSetpoint;
    
    //antiWindup code
    if (abs(_pidOutput) > 20){ //if the actuator is saturated
        _pidController.SetTunings(_Kp, _KiFar, _Kd); //disable the integration term
    }
    else{
        if (abs(_pidInput - _pidSetpoint) < .02){
            //This second check catches the corner case where the setpoint has just jumped, but compute has not been run yet
            _pidController.SetTunings(_Kp, _KiClose, _Kd);
        }
        if (abs(_pidInput - _pidSetpoint) < .06){
            _pidController.SetTunings(_Kp, _KiMid, _Kd);
        }
    }
    
    _pidInput      =  _encoder.read()/NUMBER_OF_ENCODER_STEPS;
    _pidController.Compute();
    
    _motor.write(90 + _pidOutput);
    
}

float  Axis::error(){
    return abs((_encoder.read()/NUMBER_OF_ENCODER_STEPS) - _pidSetpoint)*_mmPerRotation;
}

int    Axis::detach(){
    
    if (_motor.attached()){
        _writeFloat(_eepromAdr, read());
        EEPROM.write(EEPROMFLAG, EEPROMVALIDDATA);
    }
    
    _motor.detach();
    
    return 1;
}

int    Axis::attach(){
     _motor.attach(1);
     return 1;
}

void   Axis::hold(){
    int timeout   = 2000;
    
    if (millis() - _timeLastMoved < timeout){
        write(_axisTarget*_mmPerRotation);
    }
    else{
        detach();
    }
    
}

void   Axis::endMove(float finalTarget){
    
    _timeLastMoved = millis();
    _axisTarget    = finalTarget/_mmPerRotation;
    
}

float  Axis::_readFloat(unsigned int addr){

//readFloat and writeFloat functions courtesy of http://www.alexenglish.info/2014/05/saving-floats-longs-ints-eeprom-arduino-using-unions/


    union{
        byte b[4];
        float f;
    } data;
    for(int i = 0; i < 4; i++)
    {
        data.b[i] = EEPROM.read(addr+i);
    }
    return data.f;
}

void   Axis::_writeFloat(unsigned int addr, float x){
    union{
        byte b[4];
        float f;
    } data;
    data.f = x;
    for(int i = 0; i < 4; i++){
        EEPROM.write(addr+i, data.b[i]);
    }
}

int    Axis::_sign(float val){
    if (val < 0) return -1;
    if (val==0) return 0;
    return 1;
}

int    Axis::_change(float val){
    if (val != _oldVal){
        _oldVal = val;
        return true;
    }
    else{
        _oldVal = val;
        return false;
    }
}

void   Axis::computeSymetryOfMotor(int speed){
    /*
    
    This function computes the difference in distance moved in one direction vs the other direction
    for a given speed input. If the motor response is perfectly linear, the result will be the same
    in each direction so the idea outcome is zero.
    
    A result > 0 indicates that the motor is moving more in the positive direction than the negative
    direction so the posBoost is too large or the negBoost is too small.
    
    */
    
    _disableAxisForTesting = true;
    attach();
    
    Serial.print("compute linearity of ");
    Serial.print(_axisName);
    
    long originalEncoderPos = _encoder.read();
    
    _motor.write(90 + speed);
    delay(1000);
    
    
    long posEncoderDelta = abs(originalEncoderPos - _encoder.read());
    originalEncoderPos = _encoder.read();
    Serial.print(" - ");
    
    _motor.write(90 - speed);
    delay(1000);
    
    long negEncoderDelta = abs(originalEncoderPos - _encoder.read());
    
    _motor.write(90);
    
    Serial.println("done");
    Serial.println(posEncoderDelta - negEncoderDelta);
    
    _disableAxisForTesting = false;
}

void   Axis::computeBoost(){
    
    _disableAxisForTesting = true;
    
    long originalEncoderPos = _encoder.read();
    
    attach();
    
    int i = 0;
    while (i < 35){
        _motor.write(90+i);
        
        delay(1000);
        Serial.print(".");
        delay(1000);
        Serial.print(".");
        
        if (abs(originalEncoderPos - _encoder.read()) > 200){
            break;
        }
        
        i++;
    }
    int posBoost = i;
    Serial.println(" ");
    
    _motor.write(90);
        
    delay(1000);
    Serial.print(".");
    delay(1000);
    Serial.print(".");
    
    i                   = 0;
    originalEncoderPos  = _encoder.read();
    while (i < 35){
        _motor.write(90-i);
        
        delay(1000);
        Serial.print(".");
        delay(1000);
        Serial.print(".");
        
        if (abs(originalEncoderPos - _encoder.read()) > 200){
            break;
        }
        
        i++;
    }
    int negBoost = i;
    _motor.write(90);
    
    Serial.println(" ");
    Serial.print(_axisName);
    Serial.print(" boost values are n:");
    Serial.print(negBoost);
    Serial.print(" and p:");
    Serial.println(posBoost);
    
    _motor.setBoost(negBoost, posBoost);
    
    _disableAxisForTesting = false;
}