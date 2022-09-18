#include "poller.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

Poller::Poller() : pfd(-1), quit(false)
{
}

Poller::~Poller()
{
  Destroy();
}

void Poller::Create()
{
  if (pfd==-1) {
    pfd = epoll_create(10);
  }
}

void Poller::Destroy()
{
  if (pfd!=-1) {
    while (!items.empty()) {
      RemovePollItem(items.front());
    }
    close(pfd);
    pfd = -1;
  }
}

void Poller::Quit()
{
  quit = true;
}

void Poller::Run()
{
  while (!quit) {
    epoll_event evs[10];
    int nfds = epoll_wait(pfd, evs, 10, -1);
    if (nfds==-1) break;

    for ( int i=0 ; i<nfds ; i++ ) {
      PollItem *p = (PollItem*)evs[i].data.ptr;
      if (p) {
        p->on_events(evs[i].events);
      }
    }
  }
}

int Poller::AddPollItem(PollItem *p, uint32_t events)
{
  if (!p->poller && p->fd!=-1) {
    epoll_event ev;
    ev.events = events;
    ev.data.ptr = p;
    if (epoll_ctl(pfd, EPOLL_CTL_ADD, p->fd, &ev)==-1) return 1;
    p->poller = this;
    items.push_back(p);
    return 0;
  }
  return 1;
}

void Poller::RemovePollItem(PollItem *p)
{
  if (p->poller) {
    items.remove(p);
    if (p->fd!=-1) {
      epoll_event ev;
      memset(&ev, 0, sizeof(ev));
      epoll_ctl(pfd, EPOLL_CTL_DEL, p->fd, &ev);
    }
    p->poller = nullptr;
  }
}

void Poller::Mod(PollItem *p, uint32_t events)
{
  if (p->poller && p->fd!=-1) {
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = p;
    epoll_ctl(pfd, EPOLL_CTL_MOD, p->fd, &ev);
  }
}


PollItem::PollItem() : fd(-1), poller(nullptr)
{
}

PollItem::~PollItem()
{
  if (poller) {
    poller->RemovePollItem(this);
  }
}
