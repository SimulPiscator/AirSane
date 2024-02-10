/*
AirSane Imaging Daemon
Copyright (C) 2018-2023 Simul Piscator

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "sanecpp.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace {
// locale-independent conversions
const std::locale clocale = std::locale("C");

} // namespace

namespace sanecpp {

double
strtod_c(const std::string& s)
{
  double d;
  std::istringstream iss(s);
  iss.imbue(clocale);
  if (iss >> d)
    return d;
  if (s == "yes" || s == "true")
    return 1;
  if (s == "no" || s == "false")
    return 0;
  return std::numeric_limits<double>::quiet_NaN();
}

std::string
dtostr_c(double d)
{
  std::ostringstream oss;
  oss.imbue(clocale);
  oss << d;
  return oss.str();
}

std::ostream log(nullptr);

option option_set::s_nulloption;

static int sane_init_refcount = 0;
static std::mutex sane_init_mutex;

void
sane_init_addref()
{
  std::lock_guard<std::mutex> lock(sane_init_mutex);
  if (++sane_init_refcount == 1) {
    log << "sane_init(nullptr, nullptr)" << std::endl;
    ::sane_init(nullptr, nullptr);
  }
}

void
sane_init_release()
{
  std::lock_guard<std::mutex> lock(sane_init_mutex);
  assert(sane_init_refcount > 0);
  if (--sane_init_refcount == 0) {
    log << "sane_exit()" << std::endl;
    ::sane_exit();
  }
}

init::init()
{
  sane_init_addref();
}

init::~init()
{
  sane_init_release();
}

option_set::option_set() {}

option_set::option_set(device_handle h)
{
  init(h);
}

void
option_set::init(device_handle h)
{
  m_device = h;
  m_options.clear();
  if (h) {
    const SANE_Option_Descriptor* desc = nullptr;
    for (int i = 1; (desc = ::sane_get_option_descriptor(h.get(), i)); ++i) {
      if (desc->name && *desc->name)
        m_options[desc->name] = option(this, desc, i);
    }
  }
}

void
option_set::reload()
{
  SANE_Handle h = m_device.get();
  const SANE_Option_Descriptor* desc = nullptr;
  for (int i = 1; (desc = ::sane_get_option_descriptor(h, i)); ++i) {
    if (desc->name && *desc->name) {
      auto j = m_options.find(desc->name);
      if (j == m_options.end())
        m_options[desc->name] = option(this, desc, i);
      else {
        j->second.m_desc = desc;
        j->second.m_index = i;
      }
    }
  }
}

std::ostream&
option_set::print(std::ostream& os) const
{
  for (const auto& opt : m_options)
    if (opt.second.is_active()) {
      os << "\n[" << opt.first << "] = ";
      if (opt.second.is_null())
        os << "null";
      else if (opt.second.is_string())
        os << "\"" << opt.second.string_value() << "\"";
      else if (opt.second.array_size() == 1)
        os << opt.second.value();
      else
        for (int i = 0; i < opt.second.array_size(); ++i)
          os << opt.second.value(i) << ' ';
    }
  return os;
}

option&
option_set::operator[](const std::string& s)
{
  auto i = m_options.find(s);
  return i == m_options.end() ? s_nulloption : i->second;
}

const option&
option_set::operator[](const std::string& s) const
{
  auto i = m_options.find(s);
  return i == m_options.end() ? s_nulloption : i->second;
}

option::option(option_set* set, const SANE_Option_Descriptor* d, SANE_Int i)
  : m_set(set)
  , m_desc(d)
  , m_index(i)
{}

option::option()
  : m_set(nullptr)
  , m_desc(nullptr)
  , m_index(0)
{}

option&
option::operator=(const std::string& s)
{
  set_value(s);
  return *this;
}

option&
option::operator=(double d)
{
  set_value(d);
  return *this;
}

bool
option::is_null() const
{
  return !m_desc;
}

int
option::array_size() const
{
  if (m_desc)
    switch (m_desc->type) {
      case SANE_TYPE_BUTTON:
      case SANE_TYPE_GROUP:
        return 0;
      case SANE_TYPE_STRING:
        return 1;
      case SANE_TYPE_INT:
      case SANE_TYPE_FIXED:
      case SANE_TYPE_BOOL:
        return m_desc->size / sizeof(SANE_Word);
    }
  return 0;
}

bool
option::is_active() const
{
  return m_desc && SANE_OPTION_IS_ACTIVE(m_desc->cap);
}

bool
option::is_settable() const
{
  return m_desc && SANE_OPTION_IS_SETTABLE(m_desc->cap);
}

bool
option::is_string() const
{
  return m_desc && m_desc->type == SANE_TYPE_STRING;
}

bool
option::is_numeric() const
{
  if (m_desc)
    switch (m_desc->type) {
      case SANE_TYPE_INT:
      case SANE_TYPE_FIXED:
      case SANE_TYPE_BOOL:
        return true;
      default:
        return false;
    }
  return false;
}

bool
option::set_value(int index, const std::string& value)
{
  if (!set_string_value(index, value))
    return set_numeric_value(index, strtod_c(value));
  return true;
}

bool
option::set_value(int index, double value)
{
  if (!set_numeric_value(index, value))
    return set_string_value(index, dtostr_c(value));
  return true;
}

std::string
option::value(int index) const
{
  if (index >= 0 && index < array_size()) {
    if (is_null() || is_string())
      return string_value(index);
    if (is_numeric()) {
      std::ostringstream oss;
      oss << dtostr_c(numeric_value(index)) << m_desc->unit;
      return oss.str();
    }
  }
  return "n/a";
}

bool
option::set_string_value(int index, const std::string& value)
{
  SANE_Handle h = m_set ? m_set->m_device.get() : nullptr;
  if (!h)
    return false;
  if (!m_desc || m_desc->type != SANE_TYPE_STRING)
    return false;
  if (index != 0)
    return false;
  if (!is_settable() || !is_active())
    return false;
  SANE_Int info = 0;
  SANE_Status status = ::sane_control_option(
    h, m_index, SANE_ACTION_SET_VALUE, const_cast<char*>(value.c_str()), &info);
  log << "[" << m_desc->name << "] := \"" << value << "\"";
  if (status != SANE_STATUS_GOOD)
    log << " -> " << status;
  else if (info & SANE_INFO_RELOAD_OPTIONS)
    log << " -> reload options";
  log << std::endl;
  if (info & SANE_INFO_RELOAD_OPTIONS)
    m_set->reload();
  return status == SANE_STATUS_GOOD;
}

std::string
option::string_value(int index) const
{
  std::string s;
  SANE_Handle h = m_set ? m_set->m_device.get() : nullptr;
  if (h && is_string() && index == 0) {
    std::vector<SANE_Char> value(m_desc->size);
    SANE_Status status = ::sane_control_option(h, m_index, SANE_ACTION_GET_VALUE, value.data(), nullptr);
    if (status == SANE_STATUS_GOOD)
      s = value.data();
    else
      log << "sane_control_option(" << h << ", " << m_index << ", SANE_ACTION_GET_VALUE) -> " << status << std::endl;
  }
  return s;
}

std::vector<std::string>
option::allowed_string_values() const
{
  std::vector<std::string> values;
  if (is_string() && m_desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
    for (const SANE_String_Const* s = m_desc->constraint.string_list; *s && **s;
         ++s)
      values.push_back(*s);
  return values;
}

bool
option::set_numeric_value(int index, double value)
{
  SANE_Handle h = m_set ? m_set->m_device.get() : nullptr;
  if (!h)
    return false;
  if (!is_numeric() || !is_settable() || !is_active())
    return false;
  SANE_Word w;
  if (m_desc->type == SANE_TYPE_FIXED)
    w = SANE_FIX(value);
  else
    w = value;
  SANE_Int info = 0;
  SANE_Status status = SANE_STATUS_GOOD;
  if (array_size() == 1 && index == 0) {
    status =
      ::sane_control_option(h, m_index, SANE_ACTION_SET_VALUE, &w, &info);
    log << "[" << m_desc->name << "] := " << value << m_desc->unit;
  } else if (index >= 0 && index < array_size()) {
    std::vector<SANE_Word> data(array_size());
    status = ::sane_control_option(
      h, m_index, SANE_ACTION_GET_VALUE, data.data(), &info);
    if (status == SANE_STATUS_GOOD) {
      data[index] = w;
      status = ::sane_control_option(
        h, m_index, SANE_ACTION_SET_VALUE, data.data(), &info);
    }
    log << "[" << m_desc->name << "][" << index << "] := " << value
        << m_desc->unit;
  } else {
    log << "invalid array index for parameter " << m_desc->name << ": "
        << index;
  }
  if (status != SANE_STATUS_GOOD)
    log << " -> " << status;
  else if (info & SANE_INFO_RELOAD_OPTIONS)
    log << " -> reload options";
  log << std::endl;
  if (info & SANE_INFO_RELOAD_OPTIONS)
    m_set->reload();
  return status == SANE_STATUS_GOOD;
}

double
option::numeric_value(int index) const
{
  double value = std::numeric_limits<double>::quiet_NaN();
  SANE_Handle h = m_set ? m_set->m_device.get() : nullptr;
  if (!h || !is_numeric())
    return value;
  if (index < 0 || index >= array_size())
    return value;
  std::vector<SANE_Word> data(array_size());
  SANE_Status status = ::sane_control_option(h, m_index, SANE_ACTION_GET_VALUE, data.data(), nullptr);
  if (status != SANE_STATUS_GOOD) {
    log << "sane_control_option(" << h << ", " << m_index << ", SANE_ACTION_GET_VALUE) -> " << status << std::endl;
    return value;
  }
  value = data[index];
  if (m_desc->type == SANE_TYPE_FIXED)
    value = SANE_UNFIX(value);
  return value;
}

std::vector<double>
option::allowed_numeric_values() const
{
  std::vector<double> values;
  if (is_numeric() && m_desc->constraint_type == SANE_CONSTRAINT_WORD_LIST) {
    for (int i = 1; i <= m_desc->constraint.word_list[0]; ++i)
      values.push_back(m_desc->constraint.word_list[i]);
    if (m_desc->type == SANE_TYPE_FIXED)
      for (auto& value : values)
        value = SANE_UNFIX(value);
  }
  return values;
}

double
option::min() const
{
  double value = std::numeric_limits<double>::quiet_NaN();
  if (m_desc) {
    switch (m_desc->constraint_type) {
      case SANE_CONSTRAINT_NONE:
      case SANE_CONSTRAINT_STRING_LIST:
        break;
      case SANE_CONSTRAINT_RANGE:
        value = m_desc->constraint.range->min;
        break;
      case SANE_CONSTRAINT_WORD_LIST:
        value = std::numeric_limits<double>::infinity();
        for (int i = 1; i <= m_desc->constraint.word_list[0]; ++i)
          value = std::min<double>(value, m_desc->constraint.word_list[i]);
        break;
    }
    if (m_desc->type == SANE_TYPE_FIXED)
      value = SANE_UNFIX(value);
  }
  return value;
}

double
option::max() const
{
  double value = std::numeric_limits<double>::quiet_NaN();
  if (m_desc) {
    switch (m_desc->constraint_type) {
      case SANE_CONSTRAINT_NONE:
      case SANE_CONSTRAINT_STRING_LIST:
        break;
      case SANE_CONSTRAINT_RANGE:
        value = m_desc->constraint.range->max;
        break;
      case SANE_CONSTRAINT_WORD_LIST:
        value = -std::numeric_limits<double>::infinity();
        for (int i = 1; i <= m_desc->constraint.word_list[0]; ++i)
          value = std::max<double>(value, m_desc->constraint.word_list[i]);
        break;
    }
    if (m_desc->type == SANE_TYPE_FIXED)
      value = SANE_UNFIX(value);
  }
  return value;
}

double
option::quant() const
{
  double value = std::numeric_limits<double>::quiet_NaN();
  if (m_desc && m_desc->constraint_type == SANE_CONSTRAINT_RANGE) {
    value = m_desc->constraint.range->quant;
    if (m_desc->type == SANE_TYPE_FIXED)
      value = SANE_UNFIX(value);
  }
  return value;
}

SANE_Unit
option::unit() const
{
  return m_desc ? m_desc->unit : SANE_UNIT_NONE;
}

device_handle
open(const std::string& name, SANE_Status* pStatus)
{
  sane_init_addref();
  log << "sane_open(" << name << ") -> ";
  SANE_Handle h;
  SANE_Status status = ::sane_open(name.c_str(), &h);
  if (pStatus)
    *pStatus = status;
  if (SANE_STATUS_GOOD == status) {
    log << h << std::endl;
    struct handle_deleter
    {
      void operator()(SANE_Handle h) const
      {
        log << "sane_close(" << h << ")" << std::endl;
        ::sane_close(h);
        sane_init_release();
      }
    };
    return std::shared_ptr<void>(h, handle_deleter());
  } else {
    log << "SANE_Status " << status << std::endl;
  }
  sane_init_release();
  return std::shared_ptr<void>();
}

device_handle
open(const device_info& info, SANE_Status* pStatus)
{
  return open(info.name, pStatus);
}

std::vector<device_info>
enumerate_devices(bool localonly)
{
  std::vector<device_info> devices;
  const SANE_Device** p;
  sane_init_addref();
  log << "sane_get_devices() ..." << std::endl;
  SANE_Status status = ::sane_get_devices(&p, localonly);
  log << "... sane_get_devices() -> SANE_Status " << status << std::endl;
  if (status == SANE_STATUS_GOOD)
    while (*p) {
      device_info info;
      info.name = (*p)->name;
      info.vendor = (*p)->vendor;
      info.model = (*p)->model;
      info.type = (*p)->type;
      devices.push_back(info);
      ++p;
    }
  sane_init_release();
  return devices;
}

session::session(const std::string& devicename)
  : m_status(SANE_STATUS_GOOD)
{
  m_device = sanecpp::open(devicename, &m_status);
  init();
}

session::session(device_handle h)
  : m_device(h)
  , m_status(h ? SANE_STATUS_GOOD : SANE_STATUS_DEVICE_BUSY)
{
  init();
}

session::~session()
{
  // SANE API documentation says that sane_cancel() must be called
  // when scanning is finished.
  // Canceling without an active session should not have any adverse effects, so
  // we simply cancel always.
  cancel();
}

session&
session::start()
{
  m_status = ::sane_start(m_device.get());
  switch (m_status) {
    case SANE_STATUS_GOOD:
      break;
    default:
      log << "sane_start(" << m_device.get() << "): " << m_status << std::endl;
  }
  if (m_status == SANE_STATUS_GOOD)
    m_status = ::sane_get_parameters(m_device.get(), &m_parameters);
  return *this;
}

session&
session::cancel()
{
  if (m_device) {
    log << "sane_cancel(" << m_device.get() << ")" << std::endl;
    ::sane_cancel(m_device.get());
  }
  return *this;
}

session&
session::read(std::vector<char>& buffer)
{
  SANE_Status status = SANE_STATUS_GOOD;
  size_t total = 0;
  SANE_Byte* p = reinterpret_cast<SANE_Byte*>(buffer.data());
  while (status == SANE_STATUS_GOOD && total < buffer.size()) {
    SANE_Int read;
    status =
      ::sane_read(m_device.get(), p + total, buffer.size() - total, &read);
    total += read;
  }
  switch (status) {
    case SANE_STATUS_GOOD:
      break;
    default:
      log << "sane_read(" << m_device.get() << "): " << status << std::endl;
  }
  m_status = status;
  return *this;
}

const session&
session::dump_options() const
{
  log << "session " << m_device.get() << " options:" << m_options
      << std::endl;
  return *this;
}

void
session::init()
{
  ::memset(&m_parameters, 0, sizeof(m_parameters));
  m_options.init(m_device);
}

std::ostream&
print(std::ostream& os, SANE_Status s)
{
  return os << ::sane_strstatus(s);
}

std::ostream&
print(std::ostream& os, SANE_Unit u)
{
  switch (u) {
    case SANE_UNIT_NONE:
      break;
    case SANE_UNIT_BIT:
      os << "bit";
      break;
    case SANE_UNIT_DPI:
      os << "dpi";
      break;
    case SANE_UNIT_MICROSECOND:
      os << "µs";
      break;
    case SANE_UNIT_MM:
      os << "mm";
      break;
    case SANE_UNIT_PERCENT:
      os << "%";
      break;
    case SANE_UNIT_PIXEL:
      os << "px";
      break;
    default:
      os << "[?]";
  }
  return os;
}

} // namespace sanecpp
