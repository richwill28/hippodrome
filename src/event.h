#ifndef EVENT_H
#define EVENT_H

#include <string>

using Thread = std::string;

enum class EventType {
  read,
  write,
  lock,
  unlock,
  fork,
  join,
  begin,
  end,
  undefined
};

using Operand = std::string;

enum class Annotation { closed, up, down, open, undefined };

struct Event {
  Thread thread;
  EventType type;
  Operand operand;
  Annotation annotation;

  Event()
      : thread{-1}, type{EventType::undefined}, operand{},
        annotation{Annotation::undefined} {}

  Event(Thread thr, EventType ty, Operand op, Annotation anno)
      : thread{thr}, type{ty}, operand{op}, annotation{anno} {}
};

#endif
