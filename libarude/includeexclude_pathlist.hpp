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

#ifndef INC_ARUDE_INCLUDEEXCLUDE_PATHLIST_HPP
#define INC_ARUDE_INCLUDEEXCLUDE_PATHLIST_HPP

#include <algorithm>
#include <vector>


namespace arude
{

///
/// Path list containing paths that should be included and paths that should be excluded.
/// This allows to for deep directory trees to exclude distinct directories.
/// It is possible to add some base include paths which can later be taken as base for recursive path traversal. While a path hyrachie is traversed, the next
/// path can be checked against the excluded paths to use it or step over.
/// This class is not meant to hold a simple list of paths, a vector of paths would suit this situation better.
///
/// To correctly fill this class from a list, first add all include paths and then all exclude paths.
/// This container is optimized for access, not for modification. This container is as thread safe as a std::vector.
///
/// \tparam P Path type
///
template<typename P>
class includexclude_pathlist final
{
// Typedefs
public:
  using path_type = P; ///< Path type
  using path_includelist_type = std::vector<path_type>; ///< Type used to hold the path include list
  using path_excludelist_type = std::vector<path_type>; ///< Type used to hold the path exclude list
  using iterator = path_includelist_type::iterator; ///< Iterator type for include list access
  using const_iterator = path_includelist_type::const_iterator; ///< Iterator type for const include list access
  using reverse_iterator = path_includelist_type::reverse_iterator; ///< Iterator type for reverse include list access
  using const_reverse_iterator = path_includelist_type::const_reverse_iterator; ///< Iterator type for reverse const include list access

// Accessors
public:
  ///
  /// Returns an iterator to the first element of the include path list.
  /// \return Iterator
  ///
  iterator begin();

  ///
  /// Returns an iterator to the first element of the include path list.
  /// \return Iterator
  ///
  const_iterator begin() const;

  ///
  /// Returns an iterator to the first element of the include path list.
  /// \return Iterator
  ///
  const_iterator cbegin() const;

  ///
  /// Returns an iterator to the element following the last element of the include path list.
  /// \return Iterator
  ///
  iterator end();

  ///
  /// Returns an iterator to the element following the last element of the include path list.
  /// \return Iterator
  ///
  const_iterator end() const;

  ///
  /// Returns an iterator to the element following the last element of the include path list.
  /// \return Iterator
  ///
  const_iterator cend() const;

  ///
  /// Returns a reverse iterator to the first element of the reversed include path list.
  /// \return Iterator
  ///
  reverse_iterator rbegin();

  ///
  /// Returns a reverse iterator to the first element of the reversed include path list.
  /// \return Iterator
  ///
  const_reverse_iterator rbegin() const;

  ///
  /// Returns a reverse iterator to the first element of the reversed include path list.
  /// \return Iterator
  ///
  const_reverse_iterator crbegin() const;

  ///
  /// Returns a reverse iterator to the element following the last element of the reversed include path list.
  /// \return Iterator
  ///
  reverse_iterator rend();

  ///
  /// Returns a reverse iterator to the element following the last element of the reversed include path list.
  /// \return Iterator
  ///
  const_reverse_iterator rend() const;

  ///
  /// Returns a reverse iterator to the element following the last element of the reversed include path list.
  /// \return Iterator
  ///
  const_reverse_iterator crend() const;

  ///
  /// Says if a path is excluded.
  ///
  /// \param p Path to test for exclusion
  /// \return True if excluded
  ///
  bool excluded(const path_type& p) const;

// Modifiers
public:
  ///
  /// Adds an include path.
  ///
  /// No live filesystem access required. Filename and extensions are lost.
  /// The path give must be an absolute path or an exception is thrown.
  ///
  /// \param p Path to include
  /// \param recursive If true, recursively removes all exclude paths which are sub paths of \a p
  ///
  void add_includepath(const path_type& p, bool recursive);

  ///
  /// Adds an exclude path.
  ///
  /// No live filesystem access required. Filename and extensions are lost.
  /// The path give must be an absolute path or an exception is thrown.
  /// An exclude path must always be a sub path of an already registered include path or this operation has no effect.
  /// No recursive flag available as all sub paths are implicitely excluded as well. If this is not desired, the dedicated paths need to be included each.
  ///
  /// \param p Path to include
  ///
  void add_excludepath(const path_type& p);

  ///
  /// Sorts the include and exclude lists by name.
  /// This might optimize access. (Simple ASCII based sort)
  ///
  void sort();

  ///
  /// Replaces a given part of the root the directories with a new one.
  ///
  /// With "/foo/bar" as old root and "/foo/rab" as new root, "/foo/bar/blubb" becomes "/foo/rab/blubb".
  /// Only directories with the old root given as parent part are affected.
  ///
  /// \param old_root Old root to replace
  /// \param new_root New root to set
  ///
  void reroot(const path_type& old_root, const path_type& new_root);

  ///
  /// Clears the contents.
  ///
  void clear();

// Implementation
private:
  ///
  /// Replaces all roots of paths in a range based for compatible container with another.
  /// \see reroot
  ///
  /// \param container Container to replace paths in
  /// \param old_root Old root to replace
  /// \param new_root New root to set
  ///
  ///
  template<typename C>
  void reroot_container(C& container, const path_type& old_root, const path_type& new_root);

// Variables
private:
  path_includelist_type include_paths_; ///< Holds a list of all include paths
  path_excludelist_type exclude_paths_; ///< Holds a list of all exclude paths
};


template<typename P>
includexclude_pathlist<P>::iterator includexclude_pathlist<P>::begin()
{
  return std::begin(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::const_iterator includexclude_pathlist<P>::begin() const
{
  return std::begin(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::const_iterator includexclude_pathlist<P>::cbegin() const
{
  return std::cbegin(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::iterator includexclude_pathlist<P>::end()
{
  return std::end(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::const_iterator includexclude_pathlist<P>::end() const
{
  return std::end(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::const_iterator includexclude_pathlist<P>::cend() const
{
  return std::cend(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::reverse_iterator includexclude_pathlist<P>::rbegin()
{
  return std::rbegin(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::const_reverse_iterator includexclude_pathlist<P>::rbegin() const
{
  return std::rbegin(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::const_reverse_iterator includexclude_pathlist<P>::crbegin() const
{
  return std::crbegin(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::reverse_iterator includexclude_pathlist<P>::rend()
{
  return std::rend(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::const_reverse_iterator includexclude_pathlist<P>::rend() const
{
  return std::rend(include_paths_);
}

template<typename P>
includexclude_pathlist<P>::const_reverse_iterator includexclude_pathlist<P>::crend() const
{
  return std::crend(include_paths_);
}

template<typename P>
bool includexclude_pathlist<P>::excluded(const path_type& p) const
{
  decltype(auto) p_str = p.has_stem() ? p.string() : decltype(p){ p }.remove_filename().string();
  for (const auto& i : exclude_paths_)
  {
    if (i.string().find(p_str) == 0)
    {
      return true;
    }
  }

  return false;
}

template<typename P>
void includexclude_pathlist<P>::add_includepath(const path_type& p, bool recursive)
{
  // Check for absolute path
  if (p.is_relative())
  {
    throw std::runtime_error{ "Include path must be absolute." };
  }

  // Check if this is a sub path of a already included path
  decltype(auto) p_str = p.has_stem() ? p.string() : decltype(p){ p }.remove_filename().string();
  auto found = false;
  for (const auto& i : include_paths_)
  {
    if (i.string().string().find(old_root_str) == 0)
    {
      found = true;
      break;
    }
  }

  // If this is not a sub path of a already included path, add it
  if (found)
  {
    include_paths_.push_back(p.has_stem() ? p : decltype(p){ p }.remove_filename());
  }

  // Check if we have exclude path or sub paths of this new include path and remove them
  for (auto iter = std::cbegin(exclude_paths_); iter != std::cend(exclude_paths_);)
  {
    if (iter->string().find(p_str) == 0)
    {
      exclude_paths_.erase(iter++);
    }
    else
    {
      ++iter;
    }
  }
}

template<typename P>
void includexclude_pathlist<P>::add_excludepath(const path_type& p)
{
  // Check for absolute path
  if (p.is_relative())
  {
    throw std::runtime_error{ "Include path must be absolute." };
  }

  decltype(auto) my_p = p;
  my_p.remove_filename();

  // Check if we have exclude path or sub paths of this new include path and remove them
  decltype(auto) p_str = my_p.string();
  exclude_paths_.erase(
    std::remove_if(std::begin(exclude_paths_), std::end(exclude_paths_),
    [&p_str](const auto& i) { return i.find(p_str) == 0; },
    std::cend(exclude_paths_));

  // Add this exact path as exclude path
  exclude_paths_.push_back(my_p);

  // Check if this is an include path or has include sub paths and remove them
  include_paths_.erase(
    std::remove_if(std::begin(include_paths_), std::end(include_paths_),
    [&p_str](const auto& i) { return i.find(p_str) == 0; },
    std::cend(include_paths_));
}

template<typename P>
void includexclude_pathlist<P>::sort()
{
  std::sort(begin(), end());
  std::sort(std::begin(exclude_paths_), std::end(exclude_paths_));
}

template<typename P>
void includexclude_pathlist<P>::reroot(const path_type& old_root, const path_type& new_root)
{
  reroot_container(include_paths_, old_root, new_root);
  reroot_container(exclude_paths_, old_root, new_root);
}

template<typename P>
void includexclude_pathlist<P>::clear()
{
  include_paths_.clear();
  exclude_paths_.clear();
}

template<typename P>
template<typename C>
void includexclude_pathlist<P>::reroot_container(C& container, const path_type& old_root, const path_type& new_root)
{
  for (auto& i : container)
  {
    decltype(auto) i_str = i.string();
    decltype(auto) old_root_str = old_root.string();

    if (i_str.string().find(old_root_str) == 0)
    {
      i = path_type{ i_str.replace(0, old_root_str.size(), new_root.string()) };
    }
  }
}

} // namespace arude

#endif // #ifndef INC_ARUDE_INCLUDEEXCLUDE_PATHLIST_HPP