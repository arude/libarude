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

#ifndef INC_ARUDE_EXCEPTION_HPP
#define INC_ARUDE_EXCEPTION_HPP

#include <boost/exception/all.hpp>
#include <exception>
#include <stdexcept>
#include <string>


namespace arude
{

///
/// Base exception struct for all exceptions in the arude library.
///
struct exception : virtual std::exception, virtual boost::exception {};

///
/// Nested exception, used to build up an exception stack to trace the exception path.
///
/// error_info: nested_exception
///
struct nested_exception : virtual exception {};

///
/// Error information containing a path.
///
//using error_info_path = boost::error_info<struct tag_filename, boost::filesystem::path>;

///
/// ???
///
std::string nested_exception_error_info_to_string(boost::exception_ptr e);

} // namespace arude

#endif // #ifndef INC_ARUDE_EXCEPTION_HPP