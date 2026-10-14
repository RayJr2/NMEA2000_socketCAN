// Linux-only deterministic tests. No AF_CAN sockets are opened.
#include "NMEA2000_SocketCAN.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <type_traits>
static int failStage=0, created=0, released=0, calls=0, checks=0;
static bool closeError=false;
static std::set<int> owned;
static std::string observedName;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << __LINE__ << ": " #x " failed\n"; std::abort(); } } while(0)
extern "C" int __real_close(int);
extern "C" int __real_fcntl(int,int,...);
extern "C" int __wrap_socket(int domain,int type,int protocol) {
    ++calls;
    CHECK(domain==PF_CAN && type==SOCK_RAW && protocol==CAN_RAW);
    if(failStage==1) {errno=EMFILE;return -1;}
    // Real Linux descriptor, harmless /dev/null instead of a CAN socket.
    int fd=::open("/dev/null",O_RDWR|O_CLOEXEC);
    CHECK(fd>=0); CHECK(owned.insert(fd).second); ++created; return fd;
}
extern "C" int __wrap_ioctl(int fd,unsigned long request,...) {
    ++calls; CHECK(owned.count(fd)); CHECK(request==SIOCGIFINDEX);
    va_list args;va_start(args,request); auto* ifr=va_arg(args,struct ifreq*);va_end(args);
    observedName=ifr->ifr_name;
    if(failStage==2) {errno=ENODEV;return -1;}
    ifr->ifr_ifindex=7;return 0;
}
extern "C" int __wrap_bind(int fd,const struct sockaddr* addr,socklen_t len) {
    ++calls; CHECK(owned.count(fd));CHECK(len==sizeof(sockaddr_can));
    CHECK(reinterpret_cast<const sockaddr_can*>(addr)->can_ifindex==7);
    if(failStage==3) {errno=EADDRNOTAVAIL;return -1;}return 0;
}
extern "C" int __wrap_fcntl(int fd,int cmd,...) {
    ++calls;CHECK(owned.count(fd));
    if((cmd==F_GETFL && failStage==4)||(cmd==F_SETFL && failStage==5)) {errno=EIO;return -1;}
    CHECK(cmd==F_GETFL || cmd==F_SETFL);
    if(cmd==F_GETFL) return __real_fcntl(fd,cmd);
    va_list args;va_start(args,cmd);int flags=va_arg(args,int);va_end(args);
    CHECK(flags&O_NONBLOCK);return __real_fcntl(fd,cmd,flags);
}
extern "C" int __wrap_close(int fd) {
    ++calls;CHECK(owned.erase(fd)==1);++released;
    CHECK(__real_close(fd)==0);
    if(closeError) {errno=EIO;return -1;}return 0;
}
extern "C" ssize_t __wrap_read(int,void*,size_t) {std::abort();}
extern "C" ssize_t __wrap_write(int,const void*,size_t) {std::abort();}
extern "C" int __wrap_select(int,fd_set*,fd_set*,fd_set*,timeval*) {std::abort();}
class Driver : public tNMEA2000_SocketCAN {
public:
    using tNMEA2000_SocketCAN::tNMEA2000_SocketCAN;
    using tNMEA2000_SocketCAN::CANOpen;
    using tNMEA2000_SocketCAN::CANGetFrame;
    using tNMEA2000_SocketCAN::CANSendFrame;
    int descriptor() const {return skt;}
};
static_assert(!std::is_copy_constructible<Driver>::value,"descriptor must have one owner");
static void closedIO(Driver& d) {
    unsigned long id=0;unsigned char len=0,buf[8]={};int before=calls;
    CHECK(!d.CANGetFrame(id,len,buf));CHECK(errno==EBADF);
    CHECK(!d.CANSendFrame(0,8,buf,false));CHECK(errno==EBADF);
    CHECK(calls==before);
}
int main() {
    int scenarios=0;
    { Driver d;CHECK(d.descriptor()==-1);closedIO(d);CHECK(d.Close());CHECK(d.Close());++scenarios; }
    for(int stage=1;stage<=5;++stage) {
        Driver d;failStage=stage;int before=created, closes=released;
        CHECK(!d.CANOpen());CHECK(errno==(stage==1?EMFILE:stage==2?ENODEV:stage==3?EADDRNOTAVAIL:EIO));
        CHECK(d.descriptor()==-1);CHECK(owned.empty());
        CHECK(created-before==(stage==1?0:1));CHECK(released-closes==(stage==1?0:1));closedIO(d);
        failStage=0;CHECK(d.CANOpen());CHECK(d.descriptor()>=0);
        int count=created;CHECK(d.CANOpen());CHECK(created==count);
        int fd=d.descriptor();CHECK(d.Close());CHECK(__real_fcntl(fd,F_GETFD)==-1 && errno==EBADF);
        CHECK(d.Close());closedIO(d);++scenarios;
    }
    { int before=released;{Driver d;CHECK(d.CANOpen());}CHECK(released==before+1);CHECK(owned.empty());++scenarios; }
    { Driver d;char name[]="testcan";d.SetCANport(name);name[0]='X';CHECK(d.CANOpen());CHECK(observedName=="testcan");
      char other[]="other";d.SetCANport(other);CHECK(errno==EBUSY);CHECK(d.Close());CHECK(d.CANOpen());CHECK(observedName=="testcan");++scenarios; }
    { Driver* d;{char name[]="initial";d=new Driver(name);name[0]='X';}CHECK(d->CANOpen());CHECK(observedName=="initial");delete d;++scenarios; }
    { Driver d;CHECK(d.CANOpen());closeError=true;CHECK(!d.Close());CHECK(errno==EIO);CHECK(d.descriptor()==-1);
      closeError=false;CHECK(d.Close());CHECK(owned.empty());++scenarios; }
    { Driver d;failStage=2;closeError=true;CHECK(!d.CANOpen());CHECK(errno==ENODEV);CHECK(owned.empty());failStage=0;closeError=false;++scenarios; }
    { Driver d;for(int n=0;n<100;++n) {CHECK(d.CANOpen());CHECK(d.Close());CHECK(owned.empty());}++scenarios; }
    CHECK(created==released);CHECK(owned.empty());
    std::cout<<scenarios<<" scenarios, "<<checks<<" checks; acquired="<<created<<", released="<<released<<"\n";
}
