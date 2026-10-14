
/*
NMEA2000_SocketCAN.cpp

2017 Copyright (c) Al Thomason   All rights reserved

Support the socketCAN access (ala, Linux, RPi)
See: https://github.com/thomasonw/NMEA2000_socketCAN
     https://github.com/ttlappalainen/NMEA2000


Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.


THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Inherited NMEA2000 object for socketCAN
See also NMEA2000 library.
*/


#include "NMEA2000_SocketCAN.h"

#include <stdio.h>
#include <iostream>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

//*****************************************************************************
//  Copy the interface name. NULL selects the historical default can0.
//  Caller storage need not remain alive after construction.
tNMEA2000_SocketCAN::tNMEA2000_SocketCAN(char* CANport)
    : tNMEA2000(), skt(-1), _CANport(NULL),
      CANportStorage(CANport ? CANport : "can0") {
    _CANport = const_cast<char*>(CANportStorage.c_str());
}

tNMEA2000_SocketCAN::~tNMEA2000_SocketCAN() {
    Close();
}

bool tNMEA2000_SocketCAN::Close() {
    if (skt < 0) return true;
    const int fd = skt;
    skt = -1;
    // On Linux close releases the descriptor even when later cleanup fails.
    // Retrying could close an unrelated descriptor reused by another thread.
    return ::close(fd) == 0;
}

void tNMEA2000_SocketCAN::SetCANport(char *CANport) {
    if (!CANport) return;
    if (skt >= 0) { errno = EBUSY; return; }
    CANportStorage = CANport;
    _CANport = const_cast<char*>(CANportStorage.c_str());
}

bool tNMEA2000_SocketCAN::CANOpen() {
    if (skt >= 0) return true;
    const int candidate = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (candidate < 0) return false;
    // Keep ownership local until every initialization operation succeeds.
    const auto fail = [candidate]() {
        const int error = errno;
        ::close(candidate);
        errno = error;
        return false;
    };
    struct ifreq ifr = {};
    struct sockaddr_can addr = {};
    strncpy(ifr.ifr_name, _CANport, sizeof(ifr.ifr_name)-1);
    if (ioctl(candidate, SIOCGIFINDEX, &ifr) < 0) return fail();
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(candidate, (struct sockaddr *)&addr, sizeof(addr)) < 0) return fail();
    const int flags = fcntl(candidate, F_GETFL, 0);
    if (flags < 0) return fail();
    if (fcntl(candidate, F_SETFL, flags | O_NONBLOCK) < 0) return fail();
    skt = candidate;
    return true;
}


//*****************************************************************************
bool tNMEA2000_SocketCAN::CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool wait_sent) {
   if (skt < 0) { errno = EBADF; return false; }
   struct can_frame frame_wr;

   frame_wr.can_id  = id | CAN_EFF_FLAG;
   frame_wr.can_dlc = len;
   memcpy(frame_wr.data, buf, 8);

   return (write(skt, &frame_wr, sizeof(frame_wr)) == sizeof(frame_wr));        // Send this frame out to the socketCAN handler

             // socketCAN works to keeping all packets in-order, so
             // no need to do anything special for wait-sent
}


//*****************************************************************************
bool tNMEA2000_SocketCAN::CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) {
    if (skt < 0) { errno = EBADF; return false; }
    struct can_frame frame_rd;
    struct timeval tv = {0, 0};                                                 // Non-blocking timout when checking
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(skt, &fds);

    if (select((skt + 1), &fds, NULL, NULL, &tv) < 0)                           // Is there a FD with something to read out there?
        return false;

    if (FD_ISSET(skt, &fds)) {                                                  // Was it our CAN file-descriptor that is ready?
        if (read(skt, &frame_rd, sizeof(frame_rd)) > 0) {
            memcpy(buf, frame_rd.data, 8);
            len = frame_rd.can_dlc;
            id  = frame_rd.can_id;
            return true;
            }
        }
    return false;

}


//*****************************************************************************
tSocketStream::tSocketStream(const char *_port) : port(-1) {
  if ( _port!=0 ) {
    port=open(_port, O_RDWR | O_NOCTTY | O_NDELAY);
  }

  if ( port!=-1 ) {
    cout << _port << " opened" << endl;
  } else {
    if ( _port!=0 ) cerr << "Failed to open port " << _port << ". Using stdout"<< endl;
  }
}

//*****************************************************************************
tSocketStream::~tSocketStream() {
  if ( port!=-1 ) {
    close(port);
  }
}


/********************************************************************
*	Other 'Bridge' functions and classes
*
*
*
**********************************************************************/
int tSocketStream::read() {
  if ( port!=-1 ) {
    return -1;
  } else {
    // Serial stream bridge -- Returns first byte if incoming data, or -1 on no available data.
    struct timeval tv = { 0L, 0L };
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(0, &fds);
    if (select(1, &fds, NULL, NULL, &tv) < 0)                                   // Check fd=0 (stdin) to see if anything is there (timeout=0)
        return -1;                                                              // Nothing is waiting for us.

   return (getc(stdin));                                                         // Something is there, go get one char of it.
  }
}



//*****************************************************************************
size_t tSocketStream::write(const uint8_t* data, size_t size) {                // Serial Stream bridge -- Write data to stream.
  if ( port!=-1 ) {
    return ::write(port,data,size);
  } else {
    size_t i;

    for (i=0; (i<size) && data[i];  i++)                                        // send chars to stdout for 'size' or until null is found.
        putc(data[i],stdout);

    return(i);
  }
}


// std::this_thread::sleep_for(std::chrono::milliseconds(x));
// http://stackoverflow.com/questions/4184468/sleep-for-milliseconds

//*****************************************************************************
void delay(const uint32_t ms) {
    usleep(ms*1000);
};


//*****************************************************************************
uint32_t millis(void) {
    struct timespec ticker;

    clock_gettime(CLOCK_MONOTONIC, &ticker);
    return ((uint32_t) ((ticker.tv_sec * 1000) + (ticker.tv_nsec / 1000000)));

};




