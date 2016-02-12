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

#include <libarude/exception.hpp>


namespace arude
{

std::string nested_exception_error_info_to_string(boost::exception_ptr e)
{
  decltype(auto) retval = std::string{};
  decltype(auto) c = boost::get_error_info<boost::errinfo_nested_exception>(e);

  if (c)
  {
    try
    {
      boost::rethrow_exception(*c);
    }
    catch (boost::exception&)
    {
      retval += boost::current_exception_diagnostic_information() + "\n";
      retval += nested_exception_error_info_to_string(boost::current_exception());
    }
    catch (...)
    { // Not derived from boost::exception
      retval += boost::current_exception_diagnostic_information() + "\n";
    }
  }

  return retval;
}

} // namespace arude