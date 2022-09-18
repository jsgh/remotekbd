#ifndef POLLER_H_
#define POLLER_H_

#include <sys/epoll.h>
#include <arpa/inet.h>

#include <list>

class PollItem;

class Poller
{
public:
  Poller();
  ~Poller();

  void Create();
  void Destroy();
  void Quit();
  void Run();

  int AddPollItem(PollItem *p, uint32_t events);
  void RemovePollItem(PollItem *p);
  void Mod(PollItem *p, uint32_t events);

  int pfd;
  bool quit;
  std::list<PollItem*> items;
};

class PollItem
{
protected:
  ~PollItem();
public:
  PollItem();
  virtual void on_events(uint32_t events) = 0;
  int fd;
  Poller *poller;
};

union sock_t {
  sockaddr sa;
  sockaddr_storage ss;
  sockaddr_in sin;
  sockaddr_in6 sin6;
};

#endif /* POLLER_H_ */
