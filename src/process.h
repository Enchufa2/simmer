#ifndef PROCESS_H
#define PROCESS_H

#include "entity.h"

// forward declarations
class Activity;
class Arrival;
class Resource;

/** 
 * Abstract class for processes, active entities that need a method run().
 */
class Process: public Entity {
public:
  Process(Simulator* sim, std::string name, int mon): Entity(sim, name, mon) {}
  virtual ~Process(){}
  virtual void run() = 0;
  virtual void activate() {}
  virtual void deactivate();
};

class Manager: public Process {
  typedef boost::function<void (int)> Setter;
  
public:
  Manager(Simulator* sim, std::string name, std::string param,
          VEC<double> duration, VEC<int> value, int period, Setter set):
    Process(sim, name, false), param(param), duration(duration), value(value), 
    period(period), set(set), index(0) {}
  
  virtual void reset() { index = 0; }
  void run();
  
private:
  std::string param;
  VEC<double> duration;
  VEC<int> value;
  int period;
  Setter set;
  unsigned int index;
};

class DelayedTask: public Process {
  typedef boost::function<void ()> Task;
  
public:
  DelayedTask(Simulator* sim, std::string name, Task task): 
    Process(sim, name, false), task(task) {}
  
  void run();
  
private:
  Task task;
};

typedef UMAP<std::string, double> Attr;

struct Order {
public:
  Order(int priority=0, int preemptible=0, bool restart=false) {
    set_priority(priority);
    set_preemptible(preemptible);
    set_restart(restart);
  }
  
  void set_priority(int value) {
    if (value < 0) {
      Rcpp::warning("`priority` level cannot be < 0, `priority` set to 0");
      value = 0;
    }
    priority = value;
    if (preemptible < priority) preemptible = priority;
  }
  int get_priority() { return priority; }
  void set_preemptible(int value) {
    if (value < priority) {
      Rcpp::warning("`preemptible` level cannot be < `priority`, `preemptible` set to %d", priority);
      value = priority;
    }
    preemptible = value;
  }
  int get_preemptible() { return preemptible; }
  void set_restart(bool value) { restart = value; }
  bool get_restart() { return restart; }
  
private:
  int priority;       /**< arrival priority */
  int preemptible;    /**< maximum priority that cannot cause preemption (>=priority) */
  bool restart;       /**< whether activity must be restarted after preemption */
};

/**
 * Generation process.
 */
class Generator: public Process {
public:
  /**
   * Constructor.
   * @param sim             a pointer to the simulator
   * @param name            the name
   * @param mon             int that indicates whether this entity must be monitored
   * @param first_activity  the first activity of a user-defined R trajectory
   * @param dist            an user-defined R function that provides random numbers
   * @param order           priority, preemptible, restart
   */
  Generator(Simulator* sim, std::string name_prefix, int mon, Activity* first_activity, 
            Rcpp::Function dist, Order order): Process(sim, name_prefix, mon), 
            count(0), first_activity(first_activity), dist(dist), order(order) {}
  
  /**
   * Reset the generator: counter, trajectory
   */
  virtual void reset() { 
    count = 0;
    Rcpp::Environment dist_env(dist.environment());
    Rcpp::Environment reset_env(dist_env[".reset"]);
    Rcpp::Function reset_fun(reset_env["reset"]);
    reset_fun();
  }
  
  void run();
  
  int get_n_generated() { return count; }
  
private:
  int count;                /**< number of arrivals generated */
  Activity* first_activity;
  Rcpp::Function dist;
  Order order;
};

/** 
 *  Arrival process.
 */
class Arrival: public Process {
  struct ArrTime {
    double start;
    double activity;
    double busy_until;
    double remaining;
    ArrTime(): start(-1), activity(0), busy_until(-1), remaining(0) {}
  };
  typedef UMAP<std::string, ArrTime> ResTime;
  typedef UMAP<int, Resource*> SelMap;
  
public:
  CLONEABLE_COUNT(Arrival)
  
  Order order;        /**< priority, preemptible, restart */
  ArrTime lifetime;   /**< time spent in the whole trajectory */

  /**
  * Constructor.
  * @param sim             a pointer to the simulator
  * @param name            the name
  * @param mon             int that indicates whether this entity must be monitored
  * @param first_activity  the first activity of a user-defined R trajectory
  * @param order           priority, preemptible, restart
  */
  Arrival(Simulator* sim, std::string name, int mon, Order order, Activity* first_activity):
    Process(sim, name, mon), clones(new int(1)), order(order), activity(first_activity) {}
  
  virtual ~Arrival() { if (!--(*clones)) delete clones; }
  
  void run();
  void activate();
  void deactivate();
  virtual void leave(std::string resource);
  virtual void terminate(bool finished);
  virtual int set_attribute(std::string key, double value);
  
  Attr* get_attributes() { return &attributes; }
  double get_remaining() { return lifetime.remaining; }
  void set_start(std::string name, double value) { restime[name].start = value; }
  void set_activity(Activity* ptr) { activity = ptr; }
  void set_activity(std::string name, double value) { restime[name].activity = value; }
  double get_activity(std::string name) { return restime[name].activity; }
  void set_selected(int id, Resource* res) { selected[id] = res; }
  Resource* get_selected(int id) { return selected[id]; }
  
protected:
  ResTime restime;    /**< time spent in resources */
  Activity* activity; /**< current activity from an R trajectory */
  Attr attributes;    /**< user-defined (key, value) pairs */
  SelMap selected;    /**< selected resource */
};

/** 
 *  Batch of arrivals.
 */
class Batched: public Arrival {
public:
  CLONEABLE_COUNT(Batched)
  
  VEC<Arrival*> arrivals;

  Batched(Simulator* sim, std::string name, Activity* batcher, bool permanent):
    Arrival(sim, name, true, Order(), batcher), permanent(permanent) {}
  
  Batched(const Batched& o): Arrival(o), arrivals(o.arrivals), permanent(o.permanent) { 
    for (unsigned int i=0; i<arrivals.size(); i++) {
      arrivals[i] = arrivals[i]->clone();
    }
  }
  
  ~Batched() { 
    foreach_ (VEC<Arrival*>::value_type& itr, arrivals)
      delete itr;
    arrivals.clear();
  }
  
  void leave(std::string resource);
  void terminate(bool finished);
  int set_attribute(std::string key, double value);
  
  bool is_permanent() { return permanent; }
  
protected:
  bool permanent;
};

#endif
