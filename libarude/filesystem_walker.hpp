///
/// This file belongs to the libarude.
///
/// \author Adrian Rudin
/// \copyright Copyright 2016 Adrian Rudin (arude).
///
/// For commercial or closed source software a commercial license must be
/// obtained. Please contact me.
///
/// This file/project is part of libarude and released under the GNU General
/// Public License for non commercial software.
///
/// libarude is free software for non commerical use. You can redistribute it
/// and / or modify it under the terms of the GNU General Public License as
/// published by the Free Software Foundation, either version 3 of the License,
/// or (at your option) any later version.
///
/// libarude is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with Foobar.If not, see <http://www.gnu.org/licenses/>.
///

#ifndef INC_ARUDE_FILESYSTEM_WALKER_HPP
#define INC_ARUDE_FILESYSTEM_WALKER_HPP

#include "libarude/includeexclude_pathlist.hpp"

#include <boost/filesystem.hpp>

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <type_traits>


namespace arude
{

///
/// ???
///
template<typename P>
class filesystem_walker final
{
// Typedefs
public:
  using path_type = P; ///< Path type
  using filter_func_type = std::function<bool(const path_type&)>; ///< Filter predicat function type
  using filefound_func_type = std::function<void(path_type)>; ///< File found handler function type

// Structors
public:
  ///
  /// Ctor.
  ///
  /// \tparam IEPL Pathlist type to allow perfect forwarding
  /// \param iepl Include path list to traverse
  ///
  template<typename IEPL>
  filesystem_walker(IEPL&& iepl);

  ///
  /// Ctor.
  ///
  /// \tparam IEPL Pathlist type to allow perfect forwarding
  /// \param iepl Include path list to traverse
  /// \param ff Filter function acting as predicate to detect files
  ///
  template<typename IEPL>
  filesystem_walker(IEPL&& iepl, filter_func_type ff);

// Accessors
public:
  ///
  /// Says if a asynchronous file walker is running.
  /// \return True if running
  ///
  bool running() noexecpt const;

  ///
  /// Says if a asynchronous file walker is paused.
  /// \return True if running
  ///
  bool paused() noexecpt const;

// Operations
public:
  ///
  /// Runs a asynchrnous file walker.
  /// If a slow walker is needed, just place a sleep inside the filefound handler which is synchronous to the walker.
  /// NOOP if already running.
  ///
  /// \param filterfound_func File found handler function
  ///
  void run(filefound_func_type filterfound_func);

  ///
  /// Pauses the walker after the next file was found (no matter if it fits the predicate).
  /// NOOP if already paused or idle.
  ///
  void pause() noexecpt;

  ///
  /// Stops the walker after the next file was found (no matter if it fits the predicate).
  /// NOOP if already stopped.
  ///
  void stop() noexecpt;

// Enums
private:
  enum class state
  {
    idle,
    running,
    paused
  };

// Implementation
private:


// Variables
private:
  std::unique_ptr<std::async> async_; ///< Uptr to async instance for asynchronous walking
  includeexclude_pathlist pathlist_; ///< Include/exclude path list
  filter_func_type filter_predicate_func_; ///< File filter predicate function
  state state_; ///< Current state of walker
  std::mutex mtx_; ///< Mutex to serialize access to state and condition variable
  std::condition_variable condition_; ///< Condition variable
};


template<typename P>
template<typename IEPL>
filesystem_walker<P>::filesystem_walker(IEPL&& iepl)
  : pathlist_{ std::forward<IEPL>(iepl) }
  , state_{ state::idle }
{
  static_assert(std::is_same<IEPL, decltype(pathlist_)::value, "First argument to the filesystem walker ctor must be a includeexclude_pathlist.");
}

template<typename P>
template<typename IEPL>
filesystem_walker<P>::filesystem_walker(IEPL&& iepl, filter_func_type ff)
  : filesystem_walker{ std::forward<IEPL>(iepl) }
  , filter_predicate_func_{ ff }
{
}

template<typename P>
bool filesystem_walker<P>::running() const
{
  std::lock_guard<decltype(mtx_)> lock{ mtx_ };
  return state_ == state::running;
}

template<typename P>
bool filesystem_walker<P>::paused() const
{
  std::lock_guard<decltype(mtx_)> lock{ mtx_ };
  return state_ == state::paused;
}

template<typename P>
void filesystem_walker<P>::run(filefound_func_type filterfound_func)
{
  if (!running())
  {
    if (!fff)
    {
      throw std::runtime_error{ "File found function must be initialized." };
    }

    async_ = std::make_unique<std::async>(std::launch::async, [this, filterfound_func]
    {
      state_ = state::running;

      for (;;)
      {
        { // Exit if state is idle
          std::lock_guard<decltype(mtx_)> lock{ mtx_ };
          if (state_ == state::idle)
          {
            return;
          }
        }

        // Loop over all base include list
        namespace fs = boost::filesystem;
        for (const auto& i : pathlist_)
        {
          // Traverse path recursively
          const auto iterEnd = fs::recursive_directory_iterator{};
          for (auto iter = fs::recursive_directory_iterator{ i }; iter != iterEnd; ++iter)
          {
            if (fs::is_directory(*iter) && pathlist_.excluded(*iter))
            {
              iter.no_push();
            }
            else if (filter_predicate_func_(*iter))
            {
              filterfound_func(*iter);
            }

            // Check if paused and wait till it isn't anymore
            std::unique_lock<decltype(mtx_)> lock{ mtx_ };
            condition_.wait(lock, [] { return running() || !paused(); });
          }
        }
      }
    });
  }
}

template<typename P>
void filesystem_walker<P>::pause()
{
  {
    std::lock_guard<decltype(mtx_)> lock{ mtx_ };
    state_ = state::paused;
  }
  condition_.notify_one();
}

template<typename P>
void filesystem_walker<P>::stop()
{
  {
    std::lock_guard<decltype(mtx_)> lock{ mtx_ };
    state_ = state::idle;
  }
  condition_.notify_one();
}

} // namespace arude

#endif // #ifndef INC_ARUDE_FILESYSTEM_WALKER_HPP