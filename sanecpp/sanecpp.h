/*
AirSane Imaging Daemon
Copyright (C) 2018-2022 Simul Piscator

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

#ifndef SANE_CPP_H
#define SANE_CPP_H

#include <iostream>
#include <map>
#include <memory>
#include <sane/sane.h>
#include <string>
#include <vector>

namespace sanecpp {

// string/number conversions using the C locale
double
strtod_c(const std::string&);
std::string
dtostr_c(double);

// sanecpp::log.rdbuf(std::cerr.rdbuf());
// will enable logging to stderr
extern std::ostream log;

struct device_info
{
  std::string name, vendor, model, type;
};
std::vector<device_info>
enumerate_devices(bool localonly = true);

typedef std::shared_ptr<void> device_handle;

device_handle
open(const std::string&, SANE_Status* = nullptr);
device_handle
open(const device_info&, SANE_Status* = nullptr);

std::ostream&
print(std::ostream&, SANE_Status);
std::ostream&
print(std::ostream&, SANE_Unit);

class option_set;

class option
{
private:
  option(option_set*, const SANE_Option_Descriptor*, SANE_Int);

public:
  option();

  option& operator=(const std::string&);
  option& operator=(double);

  operator std::string() const;
  operator double() const;

  bool is_null() const;
  bool is_active() const;
  bool is_settable() const;
  bool is_string() const;
  bool is_numeric() const;

  int array_size() const;

  bool set_value(int index, const std::string& value);
  bool set_value(const std::string& s) { return set_value(0, s); }
  bool set_value(int index, double value);
  bool set_value(double d) { return set_value(0, d); }
  std::string value(int = 0) const;

  bool set_string_value(int index, const std::string& value);
  bool set_string_value(const std::string& value)
  {
    return set_string_value(0, value);
  }
  std::string string_value(int index = 0) const;
  std::vector<std::string> allowed_string_values() const;

  bool set_numeric_value(int index, double);
  bool set_numeric_value(double value) { return set_numeric_value(0, value); }
  double numeric_value(int index = 0) const;
  std::vector<double> allowed_numeric_values() const;

  double min() const;
  double max() const;
  double quant() const;
  SANE_Unit unit() const;

private:
  friend class option_set;
  option_set* m_set;
  const SANE_Option_Descriptor* m_desc;
  SANE_Int m_index;
};

class option_set
{
  option_set(const option_set&) = delete;
  option_set& operator=(const option_set&) = delete;

public:
  option_set();
  explicit option_set(device_handle);

  void init(device_handle);
  void reload();

  std::ostream& print(std::ostream&) const;
  void clear() { m_options.clear(); }

  bool empty() const { return m_options.empty(); }
  size_t size() const { return m_options.size(); }

  option& operator[](const std::string&);
  const option& operator[](const std::string&) const;

  typedef std::map<std::string, option>::iterator iterator;
  iterator begin() { return m_options.begin(); }
  iterator end() { return m_options.end(); }

  typedef std::map<std::string, option>::const_iterator const_iterator;
  const_iterator begin() const { return m_options.begin(); }
  const_iterator end() const { return m_options.end(); }

private:
  friend class option;
  device_handle m_device;
  std::map<std::string, option> m_options;
  static option s_nulloption;
};

class session
{
public:
  explicit session(const std::string& devicename);
  explicit session(device_handle);
  ~session();

  option_set& options() { return m_options; }
  const option_set& options() const { return m_options; }

  SANE_Status status() const { return m_status; }
  const SANE_Parameters* parameters() const { return &m_parameters; }

  session& start();
  session& cancel();
  session& read(std::vector<char>&);

private:
  void init();

  device_handle m_device;
  option_set m_options;
  SANE_Status m_status;
  SANE_Parameters m_parameters;
};

} // namespace sanecpp

inline std::ostream&
operator<<(std::ostream& os, SANE_Status status)
{
  return sanecpp::print(os, status);
}

inline std::ostream&
operator<<(std::ostream& os, SANE_Unit unit)
{
  return sanecpp::print(os, unit);
}

inline std::ostream&
operator<<(std::ostream& os, const sanecpp::option_set& opt)
{
  return opt.print(os);
}

#endif // SANE_CPP_H
