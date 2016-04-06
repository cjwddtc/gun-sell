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
  sqlite3_stmt *a;
  void bind_(std::string &str, int n) {
    char *ptr = (char *)malloc(str.size() + 1);
    strcpy(ptr, str.c_str());
    assert(sqlite3_bind_text(a, n, ptr, -1, free) == SQLITE_OK);
  }
  void bind_(int m, int n) { assert(sqlite3_bind_int(a, n, m) == SQLITE_OK); }
  void reset() {
    sqlite3_reset(a);
    sqlite3_clear_bindings(a);
  }

public:
  ~sql_st() { sqlite3_finalize(a); }
  sql_st(sqlite3 *db, std::string sql) {
    sqlite3_prepare_v2(db, sql.c_str(), -1, &a, 0);
  }
  template <size_t n = 1, class... ARG, class T> void bind(T a, ARG... arg) {
    if (n == 1) {
      reset();
    }
    bind_(a, n);
    bind<n + 1>(arg...);
  }
  template <size_t n> void bind() {}
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
    get_cost.bind(a.gid);
    assert(get_cost.run());
    cost += a.num * (int)get_cost[0];
  }
  commissions = 0;
  {
    int cost = this->cost;
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
  printf("%d,%d", commissions, cost);
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
    static sql_st get_remain(a, "select remain from good where id= ?1");
    static sql_st update_remain(a, "update good set remain= ?1 where id= ?2");
    static sql_st insert_sell(a,
                              "insert into sell values ( ?1, ?2, ?3, ?4, ?5)");
    get_remain.bind(gid);
    start();
    if (!get_remain.run()) {
      finish();
      throw select_fail_exception();
    }
    int n = get_remain[0];
    if (n < num) {
      finish();
      throw good_leak_exception();
    } else {
      update_remain.bind(n - num, gid);
      update_remain.run();
      // insert_sell.bind(gid, id, locale, num, get_date()); /*
      insert_sell.bind(locale, id, gid, num, get_date());
      insert_sell.run();
      finish();
      return 0;
    }
  }
  bills get_bill(int id) {
    static sql_st get_pay_date(a, "select pay_data from person where id= ?1");
    static sql_st update_pay_date(a,
                                  "update person set pay_data=?1 where id= ?2");
    static sql_st select_bill(
        a, "select locale,gid,num,y_m from sell where id= ?1 "
           "and y_m>=?2 and y_m<?3");
    get_pay_date.bind(id);
    start();
    get_pay_date.run();
    update_pay_date.bind(get_date(), id);
    update_pay_date.run();
    int date = get_pay_date[0];
    bills bls;
    select_bill.bind(id, date, get_date());
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
  std::string search_sell(int id, int start_date, int end_date,
                          std::string locale) {
    static sql_st search_locale_bill(a, "select gid,num,y_m from sell where "
                                        "id= ?1 and y_m>=?2 and y_m<=?3 and "
                                        "locale=?4");
    static sql_st search_bill(a, "select locale,gid,num,y_m from sell where "
                                 "id= ?1 and y_m>=?2 and y_m<=?3");
    std::stringstream ret;
    if (locale.empty()) {
      search_bill.bind(id, start_date, end_date);
      ret << "locale\tgid\tnum\ty_m\n";
      while (search_bill.run()) {
        std::string locale = search_bill[0];
        ret << locale.substr(0, 7) << "\t" << (std::string)search_bill[1]
            << "\t" << (int)search_bill[2] << "\t" << (int)search_bill[3]
            << std::endl;
        for (int i = 7; i < locale.size(); i += 7) {
          ret << locale.substr(i, 7) << std::endl;
        }
      }
    } else {
      search_locale_bill.bind(id, start_date, end_date, locale);
      ret << "gid\tnum\ty_m\n";
      while (search_locale_bill.run()) {
        ret << (std::string)search_locale_bill[0] << "\t"
            << (int)search_locale_bill[1] << "\t" << (int)search_locale_bill[2]
            << std::endl;
      }
    }
    return ret.str();
  }
  bool login(int id, std::string passwd) {
    sql_st select_paswd(a, "select password from person where id= ?1");
    select_paswd.bind(id);
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
class listener;
class execer {
  friend class listener;
  int id;
  std::array<char, 1024> ary;
  tcp::socket socket;
  listener *lis;
  execer(const execer &other) = delete;

public:
  execer(execer &&other) : lis(other.lis), socket(std::move(other.socket)) {}
  void read_data(const boost::system::error_code &ec, size_t bytes_transferred);
  void write_data(const boost::system::error_code &ec,
                  size_t bytes_transferred) {
    socket.async_read_some(buffer(ary),
                           std::bind(&execer::read_data, this, _1, _2));
  }
  void new_socket(const boost::system::error_code &ec);
  execer(listener *lis_, boost::asio::io_service &io_service)
      : socket(io_service), lis(lis_) {}
  std::string exec(const std::string &str) {
    char_separator<char> sep(" \t\n", "@");
    typedef tokenizer<char_separator<char>> tknz;
    tknz tok(str, sep);
    std::vector<std::string> vec(tok.begin(), tok.end());
    if (vec.size() == 0) {
      return "wrong input";
    }
    auto it = vec.begin();
    try {
      if (*it == "connect") {
        return "connect success";
      }
      if (*it == "exit") {
        throw std::string("exit");
      }
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
      if (*it == "search") {
        if (vec.size() == 1) {
          return ser.search_sell(id, 0, INT_MAX, std::string());
        } else if (vec.size() == 2) {
          int date = lexical_cast<int>(*++it);
          return ser.search_sell(id, date, date, std::string());
        } else {
          int start = lexical_cast<int>(*++it);
          int end = lexical_cast<int>(*++it);
          std::string local;
          if (vec.size() == 4) {
            local = *++it;
          }
          if (vec.size() > 4) {
            return "wrong input";
          }
          return ser.search_sell(id, start, end, local);
        }
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
};
class listener {
  boost::asio::io_service io_service;
  tcp::acceptor acceptor;
  std::thread thr;
  std::set<execer *> avli;
  bool is_listen;

public:
  listener(int n = 1)
      : io_service(), acceptor(io_service, tcp::endpoint(tcp::v4(), 31415)),
        thr(std::bind(&listener::monitor, this)) {
    is_listen = false;
    while (n--) {
      avli.insert(new execer(this, io_service));
    }
  }
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
        printf("%d\n", get_date());
        break;
      case 'd':
        date_off--;
        printf("%d\n", get_date());
        break;
      }
    }
  }
  void unlisten() { is_listen = false; }
  void listen() {
    if (!is_listen && !avli.empty()) {
      printf("keep listening\n");
      execer *ex = *avli.begin();
      acceptor.async_accept(ex->socket, std::bind(&execer::new_socket, ex, _1));
      is_listen = true;
      avli.erase(avli.begin());
    }
  }
  void add(execer *a) {
    printf("%p:close\n", a);
    avli.insert(a);
    listen();
  }
  void run() {
    listen();
    io_service.run();
  }
  ~listener() {
    thr.join();
    for (execer *ptr : avli) {
      delete ptr;
    }
  }
};

void execer::new_socket(const boost::system::error_code &ec) {
  id = -1;
  lis->unlisten();
  lis->listen();
  printf("%p:connect build\n", this);
  socket.async_read_some(buffer(ary),
                         std::bind(&execer::read_data, this, _1, _2));
}
void execer::read_data(const boost::system::error_code &ec,
                       size_t bytes_transferred) {
  if (bytes_transferred == 1024) {
    throw "";
  }
  if (!bytes_transferred) {
    socket.close();
    lis->add(this);
    return;
  }
  ary[bytes_transferred] = 0;
  printf("%p:get data:%s\n", this, ary.data());
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
    lis->add(this);
  }
}
bool qwe = true;
int main(int args, char *arg[]) {
  if (args != 2) {
    fprintf(stderr, "need max sockets\n");
    return 1;
  }
  std::string str;
  listener a(atoi(arg[1]));
  a.run();
  /*
 sqlite3 *a;
 sqlite3_open("qwe.db", &a);
 sql_st get_remain(a, "select remain from good where id= ?1");
 get_remain.bind("locks");
 printf("%d", get_remain.run());*/
}
