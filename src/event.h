#include <string>

using Thread = int;

enum class EventType { read, write, lock, unlock, fork, join, begin, end };

using Operand = std::string;

enum class Annotation { closed, up, down, open, undefined };

struct Event {
  Thread thread;
  EventType type;
  Operand operand;
  Annotation annotation;

  Event(Thread thr, EventType ty, Operand op, Annotation anno)
      : thread{thr}, type{ty}, operand{op}, annotation{anno} {}
};
