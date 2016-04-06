#include "precom.hpp"
int date_off = 0;
int get_date() {
  time_t t;
  time(&t);
  struct tm *m = gmtime(&t);
  return m->tm_year * 12 + m->tm_mon + date_off;
}
class bill;
class sell_info {
  friend class bill;
  friend std::ostream &operator<<(std::ostream &, const sell_info &b);
  std::string locale;
  std::string gid;
  int num;

public:
  sell_info(std::string locale_, std::string gid_, int num_)
      : locale(locale_), gid(gid_), num(num_) {}
};

std::ostream &operator<<(std::ostream &stream, const sell_info &b) {
  stream << b.locale.substr(0, 7) << "\t" << b.gid << "\t" << b.num << "\n";
  for (int i = 7; i < b.locale.size(); i += 7) {
    stream << b.locale.substr(i, 7) << "\n";
  }
  return stream;
}

class bill {
  int date;
  mutable std::vector<sell_info> vec;
  friend std::ostream &operator<<(std::ostream &, const bill &b);
  mutable int cost;
  mutable int commissions;

public:
  bill(int date_) : date(date_) {}
  void insert(std::string local, std::string gid, int num) const {
    vec.push_back(sell_info(local, gid, num));
  }
  bool operator<(const bill &a) const { return date < a.date; }
  void gen_cost(sqlite3 *db) const;
};
std::ostream &operator<<(std::ostream &stream, const bill &b) {
  stream << "date:\t" << b.date / 12 + 1900 << "," << b.date % 12 + 1
         << std::endl;
  stream << "total:" << b.cost << std::endl;
  stream << "commissions:" << b.commissions << std::endl;
  for (const sell_info &a : b.vec) {
    stream << a;
  }
  stream << "------------------------------------\n";
  return stream;
}

class bills {
  friend std::ostream &operator<<(std::ostream &, const bills &b);
  std::set<bill> bs;

public:
  void insert(std::string local, std::string gid, int num, int date) {
    auto it = bs.find(bill(date));
    if (it == bs.end()) {
      bill b(date);
      b.insert(local, gid, num);
      bs.insert(b);
    } else {
      it->insert(local, gid, num);
    }
  }
  void gen_cost(sqlite3 *db) {
    for (const bill &a : bs) {
      a.gen_cost(db);
    }
  }
};
std::ostream &operator<<(std::ostream &stream, const bills &b) {
  for (const bill &a : b.bs) {
    stream << a;
  }
  return stream;
}

class run_sql_fail_exception : public std::exception {
public:
  std::string str;
  run_sql_fail_exception(std::string str_) : str(str_) {}
};
class sql_st {
public:
  sqlite3_stmt *a;
  ~sql_st() { sqlite3_finalize(a); }
  sql_st(sqlite3 *db, std::string sql) {
    sqlite3_prepare_v2(db, sql.c_str(), -1, &a, 0);
  }
  void reset() {
    sqlite3_reset(a);
    sqlite3_clear_bindings(a);
  }
  void bind(std::string str, int n) {
    assert(sqlite3_bind_text(a, n, str.c_str(), -1, 0) == SQLITE_OK);
  }
  void bind(int m, int n) {
    if (sqlite3_bind_int(a, n, m) != SQLITE_OK) {
      fprintf(stderr, "%s\n", sqlite3_errmsg(sqlite3_db_handle(a)));
      throw sqlite3_errmsg(sqlite3_db_handle(a));
    }
  }
  class proxy {
    sqlite3_value *value;

  public:
    proxy(sqlite3_value *value_) : value(value_) {}
    operator std::string() {
      return std::string((const char *)sqlite3_value_text(value));
    }
    operator int() { return sqlite3_value_int(value); }
  };
  bool run() {
    while (true) {
      switch (sqlite3_step(a)) {
      case SQLITE_BUSY:
        break;
      case SQLITE_DONE:
        return false;
      case SQLITE_ROW:
        return true;
      default:
        throw(run_sql_fail_exception(
            std::string(sqlite3_errmsg(sqlite3_db_handle(a)))));
        break;
      }
    }
  }
  proxy operator[](int n) { return proxy(sqlite3_column_value(a, n)); }
};
void bill::gen_cost(sqlite3 *db) const {
  static sql_st get_cost(db, "select cost from good where id=?1");
  cost = 0;
  for (sell_info &a : vec) {
    get_cost.reset();
    get_cost.bind(a.gid, 1);
    assert(get_cost.run());
    cost += a.num * (int)get_cost[0];
  }
  commissions = 0;
  if (cost > 1800) {
    commissions += (cost - 1800) * 0.2;
    cost = 1800;
  }
  if (cost > 1000) {
    commissions += (cost - 1000) * 0.15;
    cost = 1000;
  }
  commissions += cost * 0.1;
}
class good_leak_exception : public std::exception {};
class select_fail_exception : public std::exception {};
class server {
  sqlite3 *a;

public:
  server(std::string path) {
    sqlite3_open(path.c_str(), &a);
    sqlite3_exec(a, "PRAGMA foreign_keys = ON;", 0, 0, 0);
  }
  inline void start() { sqlite3_exec(a, "begin;", 0, 0, 0); }
  inline void finish() { sqlite3_exec(a, "commit;", 0, 0, 0); }
  int add_sell(int id, std::string gid, int num, std::string locale) {
    sql_st get_remain(a, "select remain from good where id= ?1");
    sql_st update_remain(a, "update good set remain= ?1 where id= ?2");
    sql_st insert_sell(a, "insert into sell values ( ?1, ?2, ?3, ?4, ?5)");
    get_remain.reset();
    get_remain.bind(gid, 1);
    start();
    if (!get_remain.run()) {
      throw select_fail_exception();
    }
    int n = get_remain[0];
    if (n < num) {
      finish();
      throw good_leak_exception();
    } else {
      update_remain.reset();
      update_remain.bind(n - num, 1);
      update_remain.bind(gid, 2);
      update_remain.run();
      // insert_sell.bind(gid, id, locale, num, get_date()); /*
      insert_sell.reset();
      insert_sell.bind(locale, 1);
      insert_sell.bind(id, 2);
      insert_sell.bind(gid, 3);
      insert_sell.bind(num, 4);
      insert_sell.bind(get_date(), 5);
      insert_sell.run();
      finish();
      return 0;
    }
  }
  bills get_bill(int id) {
    sql_st get_pay_date(a, "select pay_data from person where id= ?1");
    sql_st update_pay_date(a, "update person set pay_data=?1 where id= ?2");
    sql_st select_bill(a, "select locale,gid,num,y_m from sell where id= ?1 "
                          "and y_m>?2 and y_m<?3");
    get_pay_date.reset();
    get_pay_date.bind(id, 1);
    start();
    get_pay_date.run();
    update_pay_date.reset();
    update_pay_date.bind(get_date(), 1);
    update_pay_date.bind(id, 2);
    update_pay_date.run();
    int date = get_pay_date[0];
    bills bls;
    select_bill.reset();
    select_bill.bind(id, 1);
    select_bill.bind(date, 2);
    std::cout << "date" << date << std::endl;
    std::cout << "date" << get_date() << std::endl;
    select_bill.bind(get_date(), 3);
    while (select_bill.run()) {
      std::string locale = select_bill[0];
      std::string gid = select_bill[1];
      int b = select_bill[2];
      int c = select_bill[3];
      bls.insert(locale, gid, b, c);
    };
    finish();
    bls.gen_cost(a);
    return bls;
  }
  bool login(int id, std::string passwd) {
    sql_st select_paswd(a, "select password from person where id= ?1");
    select_paswd.reset();
    select_paswd.bind(id, 1);
    if (!select_paswd.run()) {
      return false;
    }
    return passwd.compare(select_paswd[0]) == 0;
  }
};
server ser("qwe.db");
using boost::tokenizer;
using boost::lexical_cast;
using boost::char_separator;
using boost::asio::ip::tcp;
using boost::asio::buffer;
using std::placeholders::_1;
using std::placeholders::_2;

class execer {
  int id;
  std::array<char, 1024> ary;
  boost::asio::io_service io_service;
  tcp::socket socket;
  tcp::acceptor acceptor;
  void read_data(const boost::system::error_code &ec, size_t bytes_transferred);
  void write_data(const boost::system::error_code &ec,
                  size_t bytes_transferred) {
    socket.async_read_some(buffer(ary),
                           std::bind(&execer::read_data, this, _1, _2));
  }
  void new_socket(const boost::system::error_code &ec) {
    id = -1;
    socket.async_read_some(buffer(ary),
                           std::bind(&execer::read_data, this, _1, _2));
  }
  std::thread thr;

public:
  execer()
      : io_service(), socket(io_service),
        acceptor(io_service, tcp::endpoint(tcp::v4(), 31415)),
        thr(std::bind(&execer::monitor, this)) {}
  void monitor() {
    char ch;
    while (true) {
      std::cin.read(&ch, 1);
      switch (ch) {
      case 'q':
        io_service.stop();
        return;
      case 'i':
        date_off++;
        break;
      case 'd':
        date_off--;
        break;
      }
    }
  }
  void listen() {
    acceptor.async_accept(socket, std::bind(&execer::new_socket, this, _1));
    io_service.run();
  }
  std::string exec(const std::string &str) {
    char_separator<char> sep(" \t\n", "@");
    typedef tokenizer<char_separator<char>> tknz;
    tknz tok(str, sep);
    std::vector<std::string> vec(tok.begin(), tok.end());
    printf("%d\n", vec.size());
    if (vec.size() == 0) {
      return "wrong input";
    }
    auto it = vec.begin();
    try {
      if (*it == "login") {
        if (id != -1) {
          return "you are login";
        }
        if (vec.size() != 4) {
          return "wrong input";
        }
        id = lexical_cast<int>(*++it);
        if (*++it != "@")
          return "wrong input";
        if (ser.login(id, *++it))
          return "login success";
        else {
          id = -1;
          return "login false";
        }
      }
      if (*it == "logout") {
        if (id != -1) {
          id = -1;
          return "logout success";
        } else {
          return "you are not logout";
        }
      }
      if (id == -1) {
        return "please login";
      }
      if (*it == "sell") {
        if ((vec.size() & 0x1 == 1) && vec.size() >= 4) {
          return "wrong input";
        }
        std::string locale;
        std::string gid;
        int num;
        locale = *++it;
        gid = *++it;
        num = lexical_cast<int>(*++it);
        ser.add_sell(id, gid, num, locale);
        while (++it != vec.end()) {
          gid = *it++;
          num = lexical_cast<int>(*it);
          ser.add_sell(id, gid, num, locale);
        }
        return "sell success";
      }
      if (*it == "bill") {
        std::stringstream b;
        b << ser.get_bill(id);
        return b.str();
      }
      if (*it == "exit") {
        throw std::string("exit");
      }
    } catch (boost::bad_lexical_cast &e) {
      return std::string("illegal input:") + e.what();
    } catch (good_leak_exception) {
      return "no enought good";
    } catch (run_sql_fail_exception e) {
      return e.str;
    } catch (select_fail_exception) {
      return "wrong input,select data fail";
    }
    return "null";
  }
  ~execer() { thr.join(); }
};

void execer::read_data(const boost::system::error_code &ec,
                       size_t bytes_transferred) {
  if (bytes_transferred == 1024) {
    throw "";
  }
  if (!bytes_transferred) {
    socket.close();
    io_service.stop();
    io_service.reset();
    listen();
    return;
  }
  try {
    std::string str_ =
        exec(std::string(ary.begin(), ary.begin() + bytes_transferred));
    std::string str = std::to_string(id);
    str += ' ';
    str += str_;
    socket.async_write_some(buffer(str),
                            std::bind(&execer::write_data, this, _1, _2));
  } catch (std::string) {
    id == -1;
    std::string str = std::to_string(-2);
    str += '\0';
    boost::asio::write(socket, boost::asio::buffer(str));
    socket.close();
    io_service.stop();
    io_service.reset();
    listen();
  }
}
bool qwe = true;
int main() {
  std::string str;
  execer a;
  a.listen();
  /*
 sqlite3 *a;
 sqlite3_open("qwe.db", &a);
 sql_st get_remain(a, "select remain from good where id= ?1");
 get_remain.bind("locks");
 printf("%d", get_remain.run());*/
}
