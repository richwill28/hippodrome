#ifndef EVENT_H
#define EVENT_H

#include <iostream>
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

std::string event_type_to_string(EventType event_type) {
  switch (event_type) {
  case EventType::read:
    return "read";
  case EventType::write:
    return "write";
  case EventType::lock:
    return "lock";
  case EventType::unlock:
    return "unlock";
  case EventType::fork:
    return "fork";
  case EventType::join:
    return "join";
  case EventType::begin:
    return "begin";
  case EventType::end:
    return "end";
  case EventType::undefined:
    return "undefined";
  default:
    std::cerr << "Invalid event type\n";
    std::exit(EXIT_FAILURE);
  }
}

using Operand = std::string;

enum class Annotation { closed, up, down, open, undefined };

std::string annotation_to_string(Annotation annotation) {
  switch (annotation) {
  case Annotation::closed:
    return "closed";
  case Annotation::up:
    return "up";
  case Annotation::down:
    return "down";
  case Annotation::open:
    return "open";
  case Annotation::undefined:
    return "undefined";
  default:
    std::cerr << "Invalid annotation\n";
    std::exit(EXIT_FAILURE);
  }
}

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

  bool conflict(const Event &other) const {
    return (type == EventType::read && other.type == EventType::write) ||
           (type == EventType::write && other.type == EventType::read) ||
           (type == EventType::write && other.type == EventType::write) ||
           (type == EventType::unlock && other.type == EventType::lock);
  }

  bool operator==(const Event &other) const {
    return (thread == other.thread) && (type == other.type) &&
           (operand == other.operand) && (annotation == other.annotation);
  }
};

template <> struct std::hash<Event> {
  std::size_t operator()(const Event &key) const {
    return std::hash<std::string>{}(
        key.thread + " " + event_type_to_string(key.type) + " " + key.operand +
        " " + annotation_to_string(key.annotation));
  }
};

#endif
