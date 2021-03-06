/// \file
/// \brief QEP/C++ port to Win32 API
/// \cond
///***************************************************************************
/// Last updated for version 5.4.0
/// Last updated on  2015-03-14
///
///                    Q u a n t u m     L e a P s
///                    ---------------------------
///                    innovating embedded systems
///
/// Copyright (C) Quantum Leaps, www.state-machine.com/licensing.
///
/// This program is open source software: you can redistribute it and/or
/// modify it under the terms of the GNU General Public License as published
/// by the Free Software Foundation, either version 3 of the License, or
/// (at your option) any later version.
///
/// Alternatively, this program may be distributed and modified under the
/// terms of Quantum Leaps commercial licenses, which expressly supersede
/// the GNU General Public License and are specifically designed for
/// licensees interested in retaining the proprietary status of their code.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program. If not, see <http://www.gnu.org/licenses/>.
///
/// Contact information:
/// Web:   www.state-machine.com/licensing
/// Email: info@state-machine.com
///***************************************************************************
/// \endcond

#ifndef QEP_PORT_HPP
#define QEP_PORT_HPP

#include <stdint.h> // Exact-width types. WG14/N843 C99, Section 7.18.1.1

#ifdef _MSC_VER
#pragma warning (disable: 4510 4512 4610)
#endif

#include "qep.hpp"    // QEP platform-independent public interface

#ifdef _MSC_VER
#pragma warning (default: 4510 4512 4610)
#endif

#endif // QEP_PORT_HPP
