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

namespace arude
{
namespace noncopyable_  // Protection from unintended ADL
{

///
/// This is a helper class making a derived class noncopyable if inherited from.
///
/// Example:
/// class Derived : public noncopyable {};
///
class noncopyable
{
// Structors
protected:
  ///
  /// Copy dtor.
  /// Deleted to not allow copy construction.
  ///
  noncopyable(const noncopyable&) = delete;
  
  ///
  /// Copy assignment operator.
  /// Deleted to not allow copy assignment.
  ///
  noncopyable& operator=(const noncopyable&) = delete;
};
  
} // namespace noncopyable_

using noncopyable = noncopyable_::noncopyable;

} // namespace arude