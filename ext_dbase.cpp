/*
   dBase extension for HHVM.
   Copyright (c) 2013-2020 SIA Intelligent Systems.
   
   Based on PECL dbase extension with the following copyright:

   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2008 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.0 of the PHP license,       |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_0.txt.                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Jim Winstead <jimw@php.net>                                  |
   +----------------------------------------------------------------------+

   Modified and ported to HHVM by Kristaps Kaupe.
*/

#include "hphp/runtime/version.h"
#include "hphp/runtime/ext/extension.h"
#include "hphp/runtime/base/array-iterator.h"
#include "hphp/runtime/base/type-array.h"
#include "hphp/util/thread-local.h"
#include <fcntl.h>
#include <boost/optional.hpp>
#include "dbf.h"

namespace HPHP {
///////////////////////////////////////////////////////////////////////////////

// Wrapper around dbhead_t* for correct destruction on destroy.
class DBaseConnection {
public:
  DBaseConnection(dbhead_t* h): dbh(h) {
    if (dbh != nullptr) {
      (*ref_count)[dbh]++;
    }
  };
  DBaseConnection(const DBaseConnection& dbc) : dbh(dbc.dbh) {
    if (dbh != nullptr) {
      (*ref_count)[dbh]++;
    }
  }
  ~DBaseConnection() {
    if (dbh != nullptr) {
      (*ref_count)[dbh]--;
      if ((*ref_count)[dbh] == 0) {
        close(dbh->db_fd);
        free_dbf_head(dbh);
      }
    }
  };
  DBaseConnection& operator=(const DBaseConnection& dbc) {
    dbh = dbc.dbh;
    if (dbh != nullptr) {
      (*ref_count)[dbh]++;
    }
    return *this;
  }
  dbhead_t* dbh;
private:
  typedef std::map<dbhead_t*,size_t> dbh_ref_count;
#ifdef DECLARE_THREAD_LOCAL
  static DECLARE_THREAD_LOCAL(dbh_ref_count, ref_count);
#else
  static THREAD_LOCAL(dbh_ref_count, ref_count);
#endif
};

#ifdef IMPLEMENT_THREAD_LOCAL
  IMPLEMENT_THREAD_LOCAL(DBaseConnection::dbh_ref_count, DBaseConnection::ref_count);
#else
  THREAD_LOCAL(DBaseConnection::dbh_ref_count, DBaseConnection::ref_count);
#endif

typedef std::map<int64_t,std::shared_ptr<DBaseConnection> > open_db_map;

#ifdef IMPLEMENT_THREAD_LOCAL
  static IMPLEMENT_THREAD_LOCAL(open_db_map, open_dbases);
#else
  static THREAD_LOCAL(open_db_map, open_dbases);
#endif

// HPHP::Array::add() was removed in HHVM 3.29, should use set() instead
#if defined HHVM_VERSION_BRANCH && HHVM_VERSION_BRANCH >= 0x031D00
  #define ARRAY_ADD(arr,key,value) arr.set(String(key), Variant(value))
  #define ARRAY_PUSH(arr,value) arr.set(arr.size(), Variant(value))
#else
  #define ARRAY_ADD(arr,key,value) arr.add(String(key), Variant(value))
  #define ARRAY_PUSH(arr,value) arr.set(arr.size(), Variant(value))
#endif

static open_db_map::iterator dbase_find_connection(int64_t dbase_identifier) {
  open_db_map::iterator it = open_dbases->find(dbase_identifier);
  if (it == open_dbases->end()) {
    raise_warning("Unable to find database for identifier %" PRId64, dbase_identifier);
  }
  return it;
}

static bool dbase_add_or_replace_record(int dbase_identifier, const Variant& record, boost::optional<int64_t> recnum) {
  if (!record.isArray()) {
    raise_warning("Argument two must be of type 'Array'");
    return false;
  }

  open_db_map::iterator it = dbase_find_connection(dbase_identifier);
  if (it != open_dbases->end()) {
    dbhead_t* dbh = it->second->dbh;
    assert(dbh != nullptr);
    Array arr_record = record.toArray();

    ssize_t num_fields = arr_record.size();

    if (num_fields != dbh->db_nfields) {
      raise_warning("Wrong number of fields specified");
      return false;
    }

    char* cp = (char*)malloc(dbh->db_rlen + 1);
    char* t_cp = cp;
    *t_cp++ = VALID_RECORD;

    dbfield_t* dbf = dbh->db_fields;
    dbfield_t* cur_f = dbf;
    for (ArrayIter arr_it(arr_record); arr_it; ++arr_it, cur_f++) {
      assert(cur_f < &dbf[num_fields]);
      //snprintf(t_cp, cur_f->db_flen+1, cur_f->db_format, Z_STRVAL(tmp));
      snprintf(t_cp, cur_f->db_flen+1, cur_f->db_format, arr_it.second().toString().c_str());
      t_cp += cur_f->db_flen;
    }

    int put_recnum;
    if (recnum) {
      put_recnum = recnum.get();
    } else {
      put_recnum = dbh->db_records++;
    }

    if (put_dbf_record(dbh, put_recnum, cp) < 0) {
      raise_warning("unable to put record at %d", put_recnum);
      free(cp);
      return false;
    }

    put_dbf_info(dbh);
    free(cp);

    return true;
  } else {
    return false;
  }
}

static Variant dbase_get_record_impl(int64_t dbase_identifier, int64_t record_number, bool assoc) {
  open_db_map::iterator it = dbase_find_connection(dbase_identifier);
  if (it != open_dbases->end()) {
    dbhead_t* dbh = it->second->dbh;
    assert(dbh != nullptr);

    char* data;
    if ((data = get_dbf_record(dbh, record_number)) == nullptr) {
      raise_warning("Tried to read bad record %" PRId64, record_number);
      return Variant(false);
    }

    dbfield_t* dbf = dbh->db_fields;

    Array return_value = Array::Create();

    char* fnp = nullptr;
    ssize_t cursize = 0;
    for (dbfield_t* cur_f = dbf; cur_f < &dbf[dbh->db_nfields]; cur_f++) {
      // get the value
      char* str_value = (char*)malloc(cur_f->db_flen + 1);

      if (cursize <= cur_f->db_flen) {
        cursize = cur_f->db_flen + 1;
        fnp = (char*)realloc(fnp, cursize);
      }
      snprintf(str_value, cursize, cur_f->db_format, get_field_val(data, cur_f, fnp));

      // now convert it to the right PHP internal type
      switch (cur_f->db_type) {
      case 'C':
      case 'D':
        if (!assoc) {
          ARRAY_PUSH(return_value, String(str_value));
        } else {
          ARRAY_ADD(return_value, cur_f->db_fname, String(str_value));
        }
        break;
      case 'I':
      case 'N':
        if (cur_f->db_fdc == 0) {
          long long int_value = strtoll(str_value, NULL, 10);
          // Large integers in dbase can be larger than long long
          if (errno == ERANGE) {
            // If the integer is too large, keep it as string
            if (!assoc) {
              ARRAY_PUSH(return_value, String(str_value));
            } else {
              ARRAY_ADD(return_value, cur_f->db_fname, String(str_value));
            }
          }
          else {
            if (!assoc) {
              ARRAY_PUSH(return_value, int_value);
            } else {
              ARRAY_ADD(return_value, cur_f->db_fname, int_value);
            }
          }
        } else {
          if (!assoc) {
            ARRAY_PUSH(return_value, atof(str_value));
          } else {
            ARRAY_ADD(return_value, cur_f->db_fname, atof(str_value));
          }
        }
        break;
      case 'F':
        if (!assoc) {
          ARRAY_PUSH(return_value, atof(str_value));
        } else {
          ARRAY_ADD(return_value, cur_f->db_fname, atof(str_value));
        }
        break;
      case 'L':
        {
          bool bool_value = false;
          if ((*str_value == 'T') || (*str_value == 'Y')) {
            bool_value = true;
          }
          if (!assoc) {
            ARRAY_PUSH(return_value, bool_value);
          } else {
            ARRAY_ADD(return_value, cur_f->db_fname, bool_value);
          }
        }
        break;
      case 'M':
        // this is a memo field. don't know how to deal with this yet
        break;
      default:
        // should deal with this in some way
        break;
      }
      free(str_value);
    }

    assert(fnp != nullptr);
    free(fnp);

    // mark whether this record was deleted
    assert(data != nullptr);
    int deleted = (data[0] == '*' ? 1 : 0);
    ARRAY_ADD(return_value, "deleted", deleted);

    free(data);

    return Variant(return_value);
  } else {
    return Variant(false);
  }
}

static bool HHVM_FUNCTION(dbase_add_record, int64_t dbase_identifier, const Variant& record) {
  return dbase_add_or_replace_record(dbase_identifier, record, boost::none);
}

static bool HHVM_FUNCTION(dbase_close, int64_t dbase_identifier) {
  open_db_map::iterator it = dbase_find_connection(dbase_identifier);
  if (it != open_dbases->end()) {
    open_dbases->erase(it);
    return true;
  } else {
    return false;
  }
}

static Variant HHVM_FUNCTION(dbase_create, const String& filename, const Variant& fields) {
  if (!fields.isArray()) {
    raise_warning("Expected array as second parameter");
    return false;
  }

  //if (php_check_open_basedir(Z_STRVAL_PP(filename) TSRMLS_CC)) {
  //  RETURN_FALSE;
  //}

  int fd;
  if ((fd = open(filename.c_str(), O_BINARY|O_RDWR|O_CREAT, 0644)) < 0) {
    raise_warning("Unable to create database (%d): %s", errno, strerror(errno));
    return false;
  }

  Array arr_fields = fields.toArray();
  ssize_t num_fields = arr_fields.size();
  if (num_fields <= 0) {
    raise_warning("Unable to create database without fields");
    close(fd);
    return false;
  }

  // have to use regular malloc() because this gets free()'d by
  // code in the dbase library.
  dbhead_t* dbh = (dbhead_t*)malloc(sizeof(dbhead_t));
  dbfield_t* dbf = (dbfield_t*)malloc(sizeof(dbfield_t) * num_fields);
  if ((dbh == nullptr) || (dbf == nullptr)) {
    raise_warning("Unable to allocate memory for header info");
    if (dbh != nullptr) {
      free(dbh);
    }
    if (dbf != nullptr) {
      free(dbf);
    }
    close(fd);
    return false;
  }

  // This will ensure close(fd) and free_dbf_head(dbh) on "return false".
  DBaseConnection dbc(dbh);

  // initialize the header structure
  dbh->db_fields = dbf;
  dbh->db_fd = fd;
  dbh->db_dbt = DBH_TYPE_NORMAL;
  strcpy(dbh->db_date, "19930818");
  dbh->db_records = 0;
  dbh->db_nfields = num_fields;
  dbh->db_hlen = sizeof(struct dbf_dhead) + 1 + num_fields * sizeof(struct dbf_dfield);

  int rlen = 1;
  // make sure that the db_format entries for all fields are set to NULL to ensure we
  // don't seg fault if there's and error and we need to call free_dbf_head() before all
  // fields have been defined.
  dbfield_t* cur_f = dbf;
  for (size_t i = 0; i < num_fields; i++, cur_f++) {
    cur_f->db_format = nullptr;
  }

  cur_f = dbf;
  int i = 0;
  for (ArrayIter arr_it(arr_fields); arr_it; ++arr_it, cur_f++, i++) {
    Array& field = arr_it.second().toArrRef();
    ArrayIter field_it(field);

    // field name
    if (!field_it) {
      raise_warning("expected field name as first element of list in field %d", i);
      return false;
    }
    const String& field_name = field_it.second().toCStrRef();
    if ((field_name.size() > 10) || (field_name.size() == 0)) {
      raise_warning("invalid field name '%s' (must be non-empty and less than or equal to 10 characters)", field_name.c_str());
      return false;
    }
    strncpy(cur_f->db_fname, field_name.c_str(), field_name.size()+1);

    // field type
    ++field_it;
    if (!field_it) {
      raise_warning("expected field type as second element of list in field %d", i);
      return false;
    }
    cur_f->db_type = toupper(field_it.second().toCStrRef().c_str()[0]);

    cur_f->db_fdc = 0;

    // verify the field length
    switch (cur_f->db_type) {
    case 'L':
      cur_f->db_flen = 1;
      break;
    case 'M':
      cur_f->db_flen = 10;
      dbh->db_dbt = DBH_TYPE_MEMO;
      // should create the memo file here, probably
      break;
    case 'D':
      cur_f->db_flen = 8;
      break;
    case 'F':
      cur_f->db_flen = 20;
      break;
    case 'N':
    case 'C':
      // field length
      ++field_it;
      if (!field_it) {
        raise_warning("expected field length as third element of list in field %d", i);
        return false;
      }
      cur_f->db_flen = field_it.second().toInt32();

      if (cur_f->db_type == 'N') {
        ++field_it;
        if (!field_it) {
          raise_warning("expected field precision as fourth element of list in field %d", i);
          return false;
        }
      }
      break;
    default:
      raise_warning("unknown field type '%c'", cur_f->db_type);
      return false;
    }
    cur_f->db_foffset = rlen;
    rlen += cur_f->db_flen;

    cur_f->db_format = get_dbf_f_fmt(cur_f);
  }

  dbh->db_rlen = rlen;
  put_dbf_info(dbh);

  // We need a copy of dbc here, because return will destroy original.
  open_dbases->insert(std::make_pair(dbh->db_fd, std::shared_ptr<DBaseConnection>(new DBaseConnection(dbc))));
  return Variant(dbh->db_fd);
}

static bool HHVM_FUNCTION(dbase_delete_record, int64_t dbase_identifier, int64_t record_number) {
  open_db_map::iterator it = dbase_find_connection(dbase_identifier);
  if (it != open_dbases->end()) {
    dbhead_t* dbh = it->second->dbh;
    assert(dbh != nullptr);
    if (record_number > dbh->db_records) {
      raise_warning("record %" PRId64 " out of bounds", record_number);
      return false;
    }
    if (del_dbf_record(dbh, record_number) < 0) {
      raise_warning("unable to delete record %" PRId64, record_number);
      return false;
    }
    put_dbf_info(dbh);
    return true;
  } else {
    return false;
  }
}

static Variant HHVM_FUNCTION(dbase_get_header_info, int64_t dbase_identifier) {
  open_db_map::iterator it = dbase_find_connection(dbase_identifier);
  if (it != open_dbases->end()) {
    dbhead_t* dbh = it->second->dbh;
    assert(dbh != nullptr);

    Array return_value = Array::Create();

    dbfield_t* dbf = dbh->db_fields;
    for (dbfield_t* cur_f = dbf; cur_f < &dbh->db_fields[dbh->db_nfields]; ++cur_f) {
      Array field = Array::Create();

      // field name
      field.set(String("name"), Variant(String(cur_f->db_fname)));

      // field type
      String field_type;
      switch (cur_f->db_type) {
      case 'C': field_type = "character"; break;
      case 'D': field_type = "date"; break;
      case 'I': field_type = "integer"; break;
      case 'N': field_type = "number"; break;
      case 'L': field_type = "boolean"; break;
      case 'M': field_type = "memo"; break;
      case 'F': field_type = "float"; break;
      default: field_type = "unknown"; break;
      }
      field.set(String("type"), Variant(field_type));

      // length of field
      field.set(String("length"), Variant(cur_f->db_flen));

      // number of decimals in field
      int precision = 0;
      if ((cur_f->db_type == 'N') || (cur_f->db_type == 'I')) {
        precision = cur_f->db_fdc;
      }
      field.set(String("precision"), Variant(precision));

      // format for printing %s etc
      field.set(String("format"), Variant(String(cur_f->db_format)));

      // offset within record
      field.set(String("offset"), Variant(cur_f->db_foffset));

      ARRAY_PUSH(return_value, field);
    }
    return Variant(return_value);
  } else {
    return Variant(false);
  }
}

static Variant HHVM_FUNCTION(dbase_get_record_with_names, int64_t dbase_identifier, int64_t record_number) {
  return dbase_get_record_impl(dbase_identifier, record_number, true);
}

static Variant HHVM_FUNCTION(dbase_get_record, int64_t dbase_identifier, int64_t record_number) {
  return dbase_get_record_impl(dbase_identifier, record_number, false);
}

static Variant HHVM_FUNCTION(dbase_numfields, int64_t dbase_identifier) {
  open_db_map::iterator it = dbase_find_connection(dbase_identifier);
  if (it != open_dbases->end()) {
    assert(it->second->dbh != nullptr);
    return Variant(it->second->dbh->db_nfields);
  } else {
    return Variant(false);
  }
}

static Variant HHVM_FUNCTION(dbase_numrecords, int64_t dbase_identifier) {
  open_db_map::iterator it = dbase_find_connection(dbase_identifier);
  if (it != open_dbases->end()) {
    assert(it->second->dbh != nullptr);
    return Variant(it->second->dbh->db_records);
  } else {
    return Variant(false);
  }
}

static Variant HHVM_FUNCTION(dbase_open, const String& filename, int64_t mode) {
  if (filename.isZero()) {
    raise_warning("The filename cannot be empty.");
    return Variant(false);
  }
  if ((mode < 0) || (mode > 3)) {
    raise_warning("Invalid access mode %" PRId64, mode);
    return Variant(false);
  }

  //if (php_check_open_basedir(Z_STRVAL_PP(dbf_name) TSRMLS_CC)) {
  //  RETURN_FALSE;
  //}

  dbhead_t* dbh = dbf_open(filename.c_str(), mode);
  if (dbh == nullptr) {
    raise_warning("unable to open database %s", filename.c_str());
    return Variant(false);
  }

  open_dbases->insert(std::make_pair(dbh->db_fd, std::shared_ptr<DBaseConnection>(new DBaseConnection(dbh))));
  return Variant(dbh->db_fd);
}

static bool HHVM_FUNCTION(dbase_pack, int64_t dbase_identifier) {
  open_db_map::iterator it = dbase_find_connection(dbase_identifier);
  if (it != open_dbases->end()) {
    dbhead_t* dbh = it->second->dbh;
    assert(dbh != nullptr);
    pack_dbf(dbh);
    put_dbf_info(dbh);
    return true;
  } else {
    return false;
  }
}

static bool HHVM_FUNCTION(dbase_replace_record, int64_t dbase_identifier, const Variant& record, int64_t record_number) {
  return dbase_add_or_replace_record(dbase_identifier, record, record_number);
}

///////////////////////////////////////////////////////////////////////////////

class dbaseExtension: public Extension {
public:
  dbaseExtension(): Extension("dbase", "5.1.0") { /* nothing */ }
  virtual void moduleInit() {
    HHVM_FE(dbase_open);
    HHVM_FE(dbase_create);
    HHVM_FE(dbase_close);
    HHVM_FE(dbase_numrecords);
    HHVM_FE(dbase_numfields);
    HHVM_FE(dbase_add_record);
    HHVM_FE(dbase_get_record);
    HHVM_FE(dbase_delete_record);
    HHVM_FE(dbase_replace_record);
    HHVM_FE(dbase_pack);
    HHVM_FE(dbase_get_record_with_names);
    HHVM_FE(dbase_get_header_info);

    loadSystemlib();
  }
} s_dbase_extension;

HHVM_GET_MODULE(dbase);

}
