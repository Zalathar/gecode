/* -*- mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*- */
/*
 *  Main authors:
 *     Christian Schulte <schulte@gecode.org>
 *
 *  Copyright:
 *     Christian Schulte, 2009
 *
 *  Last modified:
 *     $Date$ by $Author$
 *     $Revision$
 *
 *  This file is part of Gecode, the generic constraint
 *  development environment:
 *     http://www.gecode.org
 *
 *  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
 *  "Software"), to deal in the Software without restriction, including
 *  without limitation the rights to use, copy, modify, merge, publish,
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <gecode/search.hh>
#include <gecode/search/support.hh>
#include <gecode/search/worker.hh>
#include <gecode/search/parallel/path.hh>

#include <gecode/support/thread.hh>

namespace Gecode { namespace Search { namespace Parallel {

  class DfsWorker;

  /// Parallel DFS engine
  class DfsEngine : public Engine {
  protected:
    /// Search options
    const Options _opt;
    /// Array of threads
    Support::Thread* _thread;
    /// Array of worker references
    DfsWorker** _worker;
  public:
    /// Provide access to search options
    const Options& opt(void) const;
    /// Return number of workers
    unsigned int workers(void) const;
    /// Provide access to worker \a i
    DfsWorker* worker(unsigned int i) const;
    
    /// \name Commands from engine to workers and wait management
    //@{
    /// Commands from engine to workers
    enum Cmd {
      C_WORK,     ///< Perform work
      C_WAIT,     ///< Run into wait lock
      C_TERMINATE ///< Terminate
    };
  protected:
    /// The current command
    volatile Cmd _cmd;
    /// Mutex for forcing workers to wait
    Support::Mutex _m_wait;
  public:
    /// Return current command
    Cmd cmd(void) const;
    /// Block all workers
    void block(void) {
      _cmd = C_WAIT;
      _m_wait.acquire();
    }
    /// Release all workers
    void release(Cmd c) {
      _cmd = c;
      _m_wait.release();
    }
    /// Ensure that worker waits
    void wait(void) {
      _m_wait.acquire(); _m_wait.release();
    }
    //@}

    /// \name Termination control
    //@{
  protected:
    /// Mutex for access to termination information
    Support::Mutex _m_terminate;
    /// Number of not yet terminated workers
    volatile unsigned int _n_not_terminated;
    /// Event for termination (all threads have terminated)
    Support::Event _e_terminate;
  public:
    /// For worker to register termination
    void terminated(void) {
      _m_terminate.acquire();
      if (--_n_not_terminated == 0)
        _e_terminate.signal();
      _m_terminate.release();
    }
    //@}

    /// \name Search control
    //@{
  protected:
    /// Mutex for search
    Support::Mutex m_search;
    /// Event for search (solution found, no more solutions, search stopped)
    Support::Event e_search;
    /// Queue of solutions
    Support::DynamicQueue<Space*,Heap> solutions;
    /// Number of busy workers
    volatile unsigned int n_busy;
    /// Whether a worker had been stopped
    volatile bool has_stopped;
    /// Whether search state changed such that signal is needed
    bool signal(void) const;
  public:
    /// Report solution \a s
    void solution(Space* s) {
      m_search.acquire();
      bool bs = signal();
      solutions.push(s);
      if (bs)
        e_search.signal();
      m_search.release();
    }
    /// Report that worker is idle
    void idle(void) {
      m_search.acquire();
      bool bs = signal();
      n_busy--;
      if (bs && (n_busy == 0))
        e_search.signal();
      m_search.release();
    }
    /// Report that worker is busy
    void busy(void) {
      m_search.acquire();
      assert(n_busy > 0);
      n_busy++;
      m_search.release();
    }
    /// Report that worker has been stopped
    void stop(void) {
      m_search.acquire();
      bool bs = signal();
      has_stopped = true;
      if (bs)
        e_search.signal();
      m_search.release();
    }
    //@}

    /// Initialize for space \a s (of size \a sz) with options \a o
    DfsEngine(Space* s, size_t sz, const Options& o);
    /// Return next solution (NULL, if none exists or search has been stopped)
    virtual Space* next(void);
    /// Return statistics
    virtual Statistics statistics(void) const;
    /// Check whether engine has been stopped
    virtual bool stopped(void) const;
    /// Destructor
    virtual ~DfsEngine(void);
  };


  /// Parallel depth-first search worker
  class GECODE_SEARCH_EXPORT DfsWorker 
    : public Worker, public Support::Runnable {
  private:
    /// Reference to engine
    DfsEngine& engine;
    /// Mutex for access to worker
    Support::Mutex m;
    /// Current path ins search tree
    Path path;
    /// Current space being explored
    Space* cur;
    /// Distance until next clone
    unsigned int d;
    /// Whether worker is currently idle
    bool idle;
  protected:
    /// Reset engine to restart at space \a s
    void reset(Space* s);
  public:
    /// Initialize for space \a s (of size \a sz) with engine \a e
    DfsWorker(Space* s, size_t sz, DfsEngine& e);
    unsigned int number(void) const;
    /// Start execution of worker
    virtual void run(void);
    /// Hand over some work (NULL if no work available)
    Space* steal(void);
    /// Try to find some work
    void find(void);
    /// Return statistics
    Statistics statistics(void) const;
    /// Destructor
    ~DfsWorker(void);
  };


  forceinline const Options&
  DfsEngine::opt(void) const {
    return _opt;
  }
  forceinline unsigned int
  DfsEngine::workers(void) const {
    return opt().threads;
  }
  forceinline DfsWorker*
  DfsEngine::worker(unsigned int i) const {
    return _worker[i];
  }

  /*
   * Engine: command handling
   */
  forceinline DfsEngine::Cmd
  DfsEngine::cmd(void) const {
    return _cmd;
  }

  /*
   * Engine
   */
  DfsEngine::DfsEngine(Space* s, size_t sz, const Options& o)
    : _opt(o), solutions(heap) {
    // Create workers
    _worker = static_cast<DfsWorker**>
      (heap.ralloc(workers() * sizeof(DfsWorker*)));
    // The first worker gets the entire search tree
    _worker[0] = new DfsWorker(s,sz,*this);
    // All other workers start with no work
    for (unsigned int i=1; i<workers(); i++)
      _worker[i] = new DfsWorker(NULL,sz,*this);
    // Block all workers
    block();
    // Create and start threads
    _thread = static_cast<Support::Thread*>
      (heap.ralloc(workers() * sizeof(Support::Thread)));
    for (unsigned int i=0; i<workers(); i++)
      (void) new (&_thread[i]) Support::Thread(_worker[i]);
    // Initialize termination information
    _n_not_terminated = workers();
    // Initialize search information
    n_busy = workers();
    has_stopped = false;
  }

  Space* 
  DfsEngine::next(void) {
    // Invariant: the worker holds the wait mutex
    m_search.acquire();
    if (!solutions.empty()) {
      // No search to be done, take leftover solution
      Space* s = solutions.pop();
      m_search.release();
      return s;
    }
    // No more solutions or stopped?
    if ((n_busy == 0) || has_stopped) {
      m_search.release();
      return NULL;
    }
    m_search.release();
    // Okay, now search has to continue, make the guys work
    release(C_WORK);

    /*
     * Wait until a search related event has happened. It might be that
     * the event has already been signalled in the last run, but the
     * solution has been removed. So we have to try until there has
     * something new happened.
     */
    while (true) {
      std::cout << "WAITING ON SIGNAL" << std::endl;
      e_search.wait();
      std::cout << "AFTER SIGNAL" << std::endl;
      m_search.acquire();
      if (!solutions.empty()) {
        std::cout << "FOUND SOLUTION" << std::endl;
        // Report solution
        Space* s = solutions.pop();
        m_search.release();
        // Make workers wait again
        block();
        return s;
      }
      // No more solutions or stopped?
      if ((n_busy == NULL) || has_stopped) {
        std::cout << "FOUND OTHER SEARCH EVENT" << std::endl;
        m_search.release();
        // Make workers wait again
        block();
        return NULL;
      }
      m_search.release();
    }
    GECODE_NEVER;
    return NULL;
  }

  bool
  DfsEngine::signal(void) const {
    return solutions.empty() && (n_busy > 0) && !has_stopped;
  }

  Statistics 
  DfsEngine::statistics(void) const {
    Statistics s;
    return s;
  }

  bool 
  DfsEngine::stopped(void) const {
    return has_stopped;
  }

  DfsEngine::~DfsEngine(void) {
    // Release all threads
    release(C_TERMINATE);
    // Wait until all threads have in fact terminated
    _e_terminate.wait();
    // Now all threads are terminated!
    heap.rfree(_worker);
    heap.rfree(_thread);
  }



  /*
   * Worker
   */
  DfsWorker::DfsWorker(Space* s, size_t sz, DfsEngine& e)
    : Worker(sz), engine(e), d(0), idle(false) {
    if (s != NULL) {
      cur = (s->status(*this) == SS_FAILED) ? NULL : snapshot(s,engine.opt());
      if (cur == NULL)
        fail++;
    } else {
      cur = NULL;
    }
    current(s);
    current(NULL);
    current(cur);
  }

  unsigned int
  DfsWorker::number(void) const {
    unsigned int i = 0;
    while (engine.worker(i) != this)
      i++;
    return i;
  }

  void
  DfsWorker::reset(Space* s) {
    delete cur;
    path.reset();
    d = 0;
    if (s->status(*this) == SS_FAILED) {
      cur = NULL;
      Worker::reset();
    } else {
      cur = s->clone();
      Worker::reset(cur);
    }
  }

  Space*
  DfsWorker::steal(void) {
    /*
     * Make a quick whether the work is idle.
     *
     * If that is not true any longer, the worker will be asked
     * again eventually.
     */
    if (idle)
      return NULL;
    m.acquire();
    Space* s = path.steal();
    m.release();
    // Tell that there will be one more busy worker
    if (s != NULL) 
      engine.busy();
    return s;
  }
  void
  DfsWorker::find(void) {
    // Try to find new work (even if there is none)
    for (unsigned int i=0; i<engine.workers(); i++)
      if (engine.worker(i) != this) {
        std::cout << "STEAL (" << i << " -> " 
                  << number() << ")" << std::endl;
        if (Space* s = engine.worker(i)->steal()) {
          std::cout << "STOLEN (" << number() << ")" << std::endl;
          // Reset this guy
          m.acquire();
          idle = false;
          cur = s;
          m.release();
          break;
        }
      }
    std::cout << "BEFORE SLEEP (" << number() << ")" << std::endl;
    Support::Thread::sleep(10);
    std::cout << "AFTER IDLE (" << number() << ")" << std::endl;
  }
  void
  DfsWorker::run(void) {
    // Okay, we are in business, start working
    while (true) {
      std::cout << "RUN (" << number() << ")" << std::endl;
      switch (engine.cmd()) {
      case DfsEngine::C_WAIT:
        // Wait as ordered by engine
        std::cout << "WAIT (" << number() << ")" << std::endl;
        engine.wait();
        std::cout << "CONTINUE (" << number() << ")" << std::endl;
        break;
      case DfsEngine::C_TERMINATE:
        // Terminate thread
        std::cout << "TERMINATE (" << number() << ")" << std::endl;
        engine.terminated();
        std::cout << "LEAVING (" << number() << ")" << std::endl;
        return;
      case DfsEngine::C_WORK:
        {
          m.acquire();
          if (idle) {
            std::cout << "IDLE (" << number() << ")" << std::endl;
            m.release();
            find();
          } else if (cur != NULL) {
            start();
            if (stop(engine.opt().stop,path.size())) {
              // Report stop
              m.release();
              engine.stop();
            } else {
              std::cout << "EXPLORE (" << number() << ")" << std::endl;
              node++;
              switch (cur->status(*this)) {
              case SS_FAILED:
                fail++;
                delete cur;
                cur = NULL;
                Worker::current(NULL);
                m.release();
                break;
              case SS_SOLVED:
                {
                  // Deletes all pending branchings
                  (void) cur->description();
                  Space* s = cur->clone(false);
                  delete cur;
                  cur = NULL;
                  Worker::current(NULL);
                  m.release();
                  engine.solution(s);
                }
                break;
              case SS_BRANCH:
                {
                  Space* c;
                  if ((d == 0) || (d >= engine.opt().c_d)) {
                    c = cur->clone();
                    d = 1;
                  } else {
                    c = NULL;
                    d++;
                  }
                  const BranchingDesc* desc = path.push(*this,cur,c);
                  Worker::push(c,desc);
                  cur->commit(*desc,0);
                  m.release();
                }
                break;
              default:
                GECODE_NEVER;
              }
            }
          } else {
            std::cout << "RECOMPUTE (" << number() << ")" << std::endl;
            if (!path.next(*this)) {
              idle = true;
              m.release();
              // Report that worker is idle
              engine.idle();
              m.acquire();
            } else {
              cur = path.recompute(d,engine.opt().a_d,*this);
            }
            Worker::current(cur);
            m.release();
          }
        }
      break;
      }
    }
  }

  Statistics
  DfsWorker::statistics(void) const {
    Statistics s = *this;
    s.memory += path.size();
    return s;
  }

  DfsWorker::~DfsWorker(void) {
    delete cur;
    path.reset();
  }


  // Create parallel depth-first engine
  Engine* dfs(Space* s, size_t sz, const Options& o) {
        return new DfsEngine(s,sz,o);
        // return ::Gecode::Search::Sequential::dfs(s,sz,o);
  }

}}}

// STATISTICS: search-any
