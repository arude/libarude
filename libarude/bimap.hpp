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

#ifndef INC_ARUDE_BIMAP_HPP
#define INC_ARUDE_BIMAP_HPP


#include "libarude/exception.hpp"

#include <map>


namespace arude
{

///
/// Thrown in bimap when a value not mapped is looked up.
///
struct nomapping_exception : virtual exception {};

///
/// Thrown in bimap when a new mapping is added which would make a mapping non unique.
///
struct nonuniquemapping_exception : virtual exception {};

///
/// The bimapper is a bi directinal map to map between different types.
/// It is used to map between two id's or maps of different type.
/// One overloaded map function returns the correct opposite type of the supplied one.
/// The bimapper is initialized on construction and immutable afterwards.
/// See example at the bottom of this file!
///
/// \tparam LT Left map type
/// \tparam RT Right map type
///
template<typename LT, typename RT>
class bimap
{
  static_assert(!std::is_same<LT, RT>::value, "bimap can't map between equal left and right type");

// Typedefs
public:
  using left_type = LT; ///< Left bimap type
  using right_type = RT; ///< Right bimap type
  using init_map_type = std::map<const left_type, right_type>; ///< Left map type;
  using left_map_type = std::map<const left_type, const right_type>; ///< Left map type
  using right_map_type = std::map<std::reference_wrapper<const right_type>, std::reference_wrapper<const left_type>, std::less<right_type>>; ///< Right map type
  using size_type = left_map_type::size_type; ///< Size type

// Structors
public:
  ///
  /// Ctor.
  ///
  bimap() = default;

  ///
  /// Ctor.
  /// \param init Initializer list with mappings to initialize the bimap
  ///
  bimap(std::initializer_list<typename left_map_type::value_type> init)
    : m_lmap{ init }
  {
    for (const auto& i : m_lmap)
    {
      m_rmap.emplace(std::cref(i.second), std::cref(i.first));
    }
  }

  ///
  /// Copy ctor.
  /// \param rhs Rhs instance
  ///
  bimap(const bimap& rhs)
    : m_lmap{ rhs.m_lmap }
  {
    for (const auto& i : m_lmap)
    {
      m_rmap.emplace(std::cref(i.second), std::cref(i.first));
    }
  }

  ///
  /// Ctor.
  /// 
  /// \param init Map to initialize the bimap with
  ///
  bimap(const init_map_type& init)
  {
    for (const auto& i : init)
    {
      const auto retval = m_lmap.emplace(i.first, i.second);
      if (retval->second)
      {
        if (m_rmap.emplace(std::cref(retval->first->second), std::cref(retval->first->first)).second)
        {
          continue;
        }
      }

      BOOST_THROW_EXCEPTION(nonuniquemapping_exception{});
    }
  }

// Accessors
public:
  ///
  /// Map first to second type.
  /// \param v First type value
  /// \return Second type value
  ///
  right_type map(left_type v) const
  {
    try
    {
      return m_lmap.at(v);
    }
    catch (...)
    {
      BOOST_THROW_EXCEPTION(nomapping_exception{} << boost::errinfo_nested_exception(boost::current_exception()));
    }
  }

  ///
  /// Map second to first type.
  /// \param v Second type value
  /// \return First type value
  ///
  left_type map(right_type v) const
  {
    try
    {
      return m_rmap.at(v);
    }
    catch (...)
    {
      BOOST_THROW_EXCEPTION(nomapping_exception{} << boost::errinfo_nested_exception(boost::current_exception()));
    }
  }

  ///
  /// Returns the left map.
  /// \return Left map
  ///
  const left_map_type& leftmap() const
  {
    return m_lmap;
  }

  ///
  /// Returns the right map.
  /// \return Left map
  ///
  const right_map_type& rightmap() const
  {
    return m_rmap;
  }

  ///
  /// Returns the size of the bimap.
  /// \return Size
  ///
  size_type size() const
  {
    return m_lmap.size();
  }

// Modifiers
public:
  template<typename ILT, typename IRT>
  void insert(ILT&& ilv, IRT&& irv)
  {
    // Check for the possibility to mess up uniqueness
    if (m_lmap.count(ilv) != 0 || m_rmap.count(irv) != 0)
    {
      BOOST_THROW_EXCEPTION(nonuniquemapping_exception{});
    }

    const auto iter = m_lmap.emplace(std::cref(i.first), std::cref(i.second)).first;
    m_rmap.emplace(std::cref(iter->second), std::cref(iter->first));
  }

// Operations
public:
  ///
  /// Clears the bimap of all contents.
  ///
  void clear()
  {
    m_lmap.clear();
    m_rmap.clear();
  }

// Variables
private:
  left_map_type m_lmap; ///< Left map
  right_map_type m_rmap; ///< Right map
};

} // namespace arude

#endif // #ifndef INC_ARUDE_BIMAP_HPP