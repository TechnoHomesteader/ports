#include "Ports.h"

#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <lo/lo.h>
#include <lo/lo_cpp.h>

Ports::Ports() {
}

Ports::~Ports() {
	
}

lo::ServerThread oscServer(5000);

void Ports::start() {

	std::cout << "Ports: Starting\n";

	midiOutput.openDevice("/dev/snd/midiC1D0");

	pixi.configure();
	
	startOSC(5000);

	signal(SIGALRM, pixiTimerCallback);   
        ualarm(PORTS_TIMER_PERIOD, PORTS_TIMER_PERIOD);
}

void Ports::startOSC(int port) {

	oscServer.add_method(NULL, NULL,
		[this](const char *path, const char *types, lo_arg ** argv, int argc) {
			float value = 1;
			if (argc==1 && types[0]=='f'){
    			value = (float)argv[0]->f;
			}
			this->oscMessage(path, value);
			//return 0;
		}
		/*[this](const char* path, const lo::Message &msg) {
		std::cout<< "message : " << path;
		float value = 1;
		if (&msg.argc&==1 && &msg.types[0]&=='f'){
			value = &msg.argv()[0]->f&;
		}
		this->oscMessage(path, value);
	  }*/
	);
	oscServer.start();
	
	std::cout << "osc started";
}


int Ports::parseInt(const char* a, int offset) {
	int sign, n, c;
	c = 0;
	n = 0;
	if (a[offset] == '-') {  // Handle negative integers
		sign = -1;
     	offset++;
  	} else {
  		sign = 1;
  	}
  	while(a[offset]!='/' && offset<strlen(a)){
  		n = n * 10 + a[offset] - '0';
  		offset++;
  		c++;
  	}
  	if (sign == -1) {
	    n = -n;
  	}
  	return n;
}


inline void Ports::pixiTimer() {
	//check timer accuracy
    gettimeofday(&now, NULL);
    timersub(&now, &lastTimer, &elapsed);
    lastTimer = now;
	if (elapsed.tv_sec < 100000 && elapsed.tv_usec>PORTS_TIMER_PERIOD * (1+PORTS_TIMER_PERIOD_TOLERANCE)) {
    	printf("timer underrun. last timer : %f ms ago.\n", elapsed.tv_usec/1000.0);
    }
    
    //update channels
    long interval = elapsed.tv_usec;
	for(int channel = 0; channel < 20; channel++){
		//trigger mode
	  	if (PORTS_OUTPUT_MODE_TRIG == channelModes[channel]) {
			if (channelTrigCyles[channel] > 0) {
			  	//channelValues[channel] =  1;
			  	channelTrigCyles[channel]--;
			} else {
			  	channelValues[channel] = 0;
			}
			pixi.setChannelValue(channel, channelValues[channel], false, channelIsBipolar(channel));
		}

	  //check if mode is LFO kind
	  if (channelIsLfo(channel)) {
		double lfoPeriod = (1000000.0 / channelLFOFrequencies[channel]);
		channelLFOPhases[channel] += interval/lfoPeriod;
		if (channelLFOPhases[channel]>1) {
			channelLFOPhases[channel] -= 1;
		}
		double phase = channelLFOPhases[channel];
		//std::cout << "LFO " << channel << " : " << phase << "\n";
		switch (channelModes[channel]) {
		  case PORTS_OUTPUT_MODE_LFO_SINE:
			channelValues[channel] = sin(phase * 2 * M_PI) * 0.5 + 0.5; //TODO: use lookup table
			break;
		  case PORTS_OUTPUT_MODE_LFO_SAW:
			channelValues[channel] = (1 - phase);
			break;
		  case PORTS_OUTPUT_MODE_LFO_RAMP:
			channelValues[channel] = phase;
			break;
		  case PORTS_OUTPUT_MODE_LFO_TRI:
			channelValues[channel] = (phase < 0.5 ? phase * 2 : (1 - phase) * 2) * 0.5 + 0.5;
			break;
		  case PORTS_OUTPUT_MODE_LFO_SQUARE:
			channelValues[channel] = (phase < channelLFOPWMs[channel]) ? 1 : 0;
			break;
		}
		pixi.setChannelValue(channel, channelValues[channel], false, channelIsBipolar(channel));
	  }
	}
    
    //update pixi
	pixi.update();

}


void Ports::oscMessage(const char* path, float v) {
	float value = v;
	int offset = 0;
	if (strncmp(path, "/in/", 3)==0) {
		offset += 4;
		//TODO:
	} else if (strncmp(path, "/out/", 5)==0) {
		offset += 5;
		int channel = parseInt(path, offset);
		if (channel > 0 && channel<=20){
			offset += channel<10?2:channel<100?3:4;
			channel -= 1;
			int mode = parseOutputMode(path, offset);
			bool force = false;
			if (channelModes[channel] != mode) {
				channelModes[channel] = mode;
				force = true;
			}
			bool isBipolar = channelIsBipolar(channel);
			std::cout << "OSC -> " << path << " : " << value << "\n";
			if (channelIsLfo(channel)){
				//value scaling
				if (value<=0){
					value = 0.01;
				}
				if (value>1000){
					value = 1000;
				}
				channelLFOFrequencies[channel] = value;
				pixi.setChannelMode(channel, false, isBipolar, force);
			}else{
				//value scaling
				if (value > 1) {
					value = 1;
				}
				if (value < 0) {
					value = 0;
				}
				channelValues[channel] = value;
				//change pixi mode
				pixi.setChannelMode(channel, false, isBipolar, force);
				pixi.setChannelValue(channel, value, false, isBipolar);
				if (PORTS_OUTPUT_MODE_TRIG == channelModes[channel]) {
					channelTrigCyles[channel] = PORTS_TRIGGER_CYCLES;
				}
				
			}
			
		} else {
			std::cout << "invalid channel : " << path << "\n";
		}
	} else if (strncmp(path, "/midi/", 6)==0) {
		offset += 6;
		std::cout << "OSC -> " << path << " : " << value << "\n";
		midiOutput.message(path,offset, value);
	}			
}


int Ports::parseOutputMode(const char* str, int offset){
	if (strncmp(str+offset, "gate", 4)==0) {
		return PORTS_OUTPUT_MODE_GATE;
	} else if (strncmp(str+offset, "trig", 4)==0) {
		return PORTS_OUTPUT_MODE_TRIG;
	} else if (strncmp(str+offset, "flipflop", 8)==0) {
		return PORTS_OUTPUT_MODE_FLIPFLOP;
	} else if (strncmp(str+offset, "cvuni", 5)==0 || strncmp(str+offset, "cv", 2)==0) {
		return PORTS_OUTPUT_MODE_CVUNI;
	} else if (strncmp(str+offset, "cvbi", 4)==0) {
		return PORTS_OUTPUT_MODE_CVBI;
	} else if (strncmp(str+offset, "sh", 2)==0) {
		return PORTS_OUTPUT_MODE_RANDOM_SH;
	} else if (strncmp(str+offset, "lfosine", 7)==0) {
		return PORTS_OUTPUT_MODE_LFO_SINE;
	} else if (strncmp(str+offset, "lfosaw", 6)==0) {
		return PORTS_OUTPUT_MODE_LFO_SAW;
	} else if (strncmp(str+offset, "lforamp", 7)==0) {
		return PORTS_OUTPUT_MODE_LFO_RAMP;
	}else if (strncmp(str+offset, "lfotri", 6)==0) {
		return PORTS_OUTPUT_MODE_LFO_TRI;
	} else if (strncmp(str+offset, "lfosquare", 9)==0) {
		return PORTS_OUTPUT_MODE_LFO_SQUARE;
	}
	return -1;
}


int Ports::parseInputMode(const char* str, int offset){
	if (strncmp(str+offset, "gate", 4)==0) {
		return PORTS_INPUT_MODE_GATE;
	} else if (strncmp(str+offset, "trig", 4)==0) {
		return PORTS_INPUT_MODE_TRIG;
	} else if (strncmp(str+offset, "cvuni", 5)==0 || strncmp(str+offset, "cv", 2)==0) {
		return PORTS_INPUT_MODE_CVUNI;
	} else if (strncmp(str+offset, "cvbi", 4)==0) {
		return PORTS_INPUT_MODE_CVBI;
	}
	return -1;
}


bool Ports::channelIsInput(int channel) {
  return  channelModes[channel] >= 100;
}


bool Ports::channelIsLfo(int channel) {
  return channelModes[channel] > 70 && channelModes[channel] < 100;
}


bool Ports::channelIsBipolar(int channel) {
	return false;
  int modee = channelModes[channel];
  return (modee < 50 || modee >= 100 && modee < 150) ? false : true;
}


Ports portsInstance;

inline void pixiTimerCallback(int sig_num){
	portsInstance.pixiTimer();
}

/*main() {

	ports.start();

	while(true){
	  std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
    //std::thread threadLibLo(startOSCLibLo, "5002");
	//std::thread threadOSCPACK(startOSCPACK, 1);
	//signal(SIGALRM, timerCallback);   
	//this one makes udp server to stop working
    //ualarm(PORTS_TIMER_PERIOD, PORTS_TIMER_PERIOD);
    //startOSCLibLo("5000");
 	std::cout << "Ports ending.\n";
	return 0;
}*/



/*
String Ports::channelGetModeName(int channel) {
  String out = "";
  switch (channelModes[channel]) {
    case OUTPUT_MODE_GATE:
      out = "gate";
      break;
    case OUTPUT_MODE_TRIG:
      out = "trig";
      break;
    case OUTPUT_MODE_FLIPFLOP:
      out = "flipflop";
      break;
    case OUTPUT_MODE_CVUNI:
      out = "cvuni";
      break;
    case OUTPUT_MODE_CVBI:
      out = "cvbi";
      break;
    case OUTPUT_MODE_RANDOM_SH:
      out = "sh";
      break;
    case OUTPUT_MODE_LFO_SINE:
      out = "lfosine";
      break;
    case OUTPUT_MODE_LFO_SAW:
      out = "lfosaw";
      break;
    case OUTPUT_MODE_LFO_RAMP:
      out = "lforamp";
      break;
    case OUTPUT_MODE_LFO_TRI:
      out = "lfotri";
      break;
    case OUTPUT_MODE_LFO_SQUARE:
      out = "lfosquare";
      break;

    case INPUT_MODE_GATE:
      out = "gate";
      break;
    case INPUT_MODE_TRIG:
      out = "trig";
      break;
    case INPUT_MODE_CVUNI:
      out = "cvuni";
      break;
    case INPUT_MODE_CVBI:
      out = "cvbi";
      break;
  }
  return out;
}
*/









