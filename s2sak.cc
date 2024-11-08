#include <fstream>
#include <iostream>
#include <optional>
#include <regex>

#include <boost/program_options.hpp>
#include <libpq-fe.h>
#include <mysql.h>

typedef int ExitStatus;

static std::optional<std::string> Env(const char *key) {
  if (const char *raw = std::getenv(key)) {
    std::string value(raw);

    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), [](unsigned char c) {
                  return !std::isspace(c);
                }));
    value.erase(std::find_if(value.rbegin(),
                             value.rend(),
                             [](unsigned char c) { return !std::isspace(c); })
                    .base(),
                value.end());

    return value;
  }

  return std::nullopt;
}

namespace option_sp {
static ExitStatus ShowMissings(const std::vector<const char *> &missings,
                               const char *msg) {
  std::cerr << msg;
  std::copy(missings.cbegin(),
            missings.cend() - 1,
            std::ostream_iterator<std::string>(std::cerr, ", "));
  std::cerr << missings.back() << std::endl;
  return EXIT_FAILURE;
}

class Context {
public:
  int argc;
  const char **argv;
};

template <class... Ls> struct OptionLs {
  static constexpr std::size_t size = sizeof...(Ls);
};

template <class... Options> struct Dispatcher {
  template <class Option> static ExitStatus Dispatch(const Context &ctx) {
    Option cmd(ctx);
    return cmd.Run();
  }
};

template <class Option> struct Dispatcher<OptionLs<Option>> {
  static ExitStatus Dispatch(const Context &ctx, const std::string &name) {
    if (std::string_view(name) == Option::OptionInfo::name) {
      return Dispatcher<>::Dispatch<Option>(ctx);
    } else {
      std::cerr << "Unknown option: " << name << std::endl;
      return EXIT_FAILURE;
    }
  }
};

template <class Option, class... Options>
struct Dispatcher<OptionLs<Option, Options...>> {
  static ExitStatus Dispatch(const Context &ctx, const std::string &name) {
    if (name == Option::OptionInfo::name) {
      return Dispatcher<>::Dispatch<Option>(ctx);
    } else {
      return Dispatcher<OptionLs<Options...>>::Dispatch(ctx, name);
    }
  }
};

template <class... Options> struct Info {
  static constexpr std::size_t size = sizeof...(Options);
};

template <class... Options> struct Info<OptionLs<Options...>> {
  static void Show() {
    ((std::cout << "\n    " << Options::OptionInfo::name << ": "
                << Options::OptionInfo::description),
     ...);
    std::cout << std::endl;
  }
};

template <class Option> class OptionSupport {
public:
  OptionSupport(const Context &c) : ctx{c} {}

  ExitStatus Run() {
    Option &option = *static_cast<Option *>(this);
    if constexpr (requires(boost::program_options::variables_map &vm) {
                    { option.Do(vm) } -> std::same_as<ExitStatus>;
                  }) {

      boost::program_options::options_description desc{
          Option::OptionInfo::name};
      Option::AddOptions(desc);

      boost::program_options::command_line_parser parser{ctx.argc - 1,
                                                         ctx.argv + 1};
      parser.options(desc);

      boost::program_options::variables_map vm;
      try {
        boost::program_options::store(Parse(parser, option), vm);
      } catch (const boost::program_options::error &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
      }

      try {
        boost::program_options::notify(vm);
      } catch (const boost::program_options::error &e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
      }

      return option.Do(vm);
    } else if constexpr (requires {
                           { option.Do() } -> std::same_as<ExitStatus>;
                         }) {
      return option.Do();
    }
  }

protected:
  const Context &ctx;

private:
  static boost::program_options::parsed_options
  Parse(boost::program_options::command_line_parser &parser, Option &option) {
    if constexpr (requires(
                      boost::program_options::positional_options_description
                          &p) { option.AddPositional(p); }) {
      boost::program_options::positional_options_description p;
      option.AddPositional(p);
      parser.positional(p);

      return parser.run();
    } else {
      return parser.run();
    }
  }
};

class DjTestNamesOption : public OptionSupport<DjTestNamesOption> {
public:
  struct OptionInfo {
    static constexpr const char *name = "dj-test-names";
    static constexpr const char *description = "Dj test names complete script";
  };

  using OptionSupport<DjTestNamesOption>::OptionSupport;

  static void AddOptions(boost::program_options::options_description &desc) {
    desc.add_options() //
        ("input,i",
         boost::program_options::value<std::string>(),
         "Input file") //
        ("output,o",
         boost::program_options::value<std::string>(),
         "Output file");
  }

  ExitStatus Do(boost::program_options::variables_map &vm) {
    std::optional<std::string> input_opt =
        vm.count("input") ? Readfile(vm["input"].as<std::string>()) : ReadCin();

    if (!input_opt) return EXIT_FAILURE;

    std::vector<std::vector<std::string>> results;
    results.reserve(80);

    std::regex line_p(R"((test_\w+) \((\w+(?:\.\w+)+)\))");
    std::regex word_p(R"((\w+))");

    const std::string &input_str = *input_opt;
    std::transform(
        std::sregex_iterator{input_str.cbegin(), input_str.cend(), line_p},
        std::sregex_iterator{},
        std::back_inserter(results),
        [&word_p](const std::smatch &match) {
          std::vector<std::string> result;
          result.reserve(10);

          const std::string smatch = match[2];
          std::transform(
              std::sregex_iterator{smatch.cbegin(), smatch.cend(), word_p},
              std::sregex_iterator{},
              std::back_inserter(result),
              [](const std::smatch &m) { return m.str(); });
          result.emplace_back(match[1]);

          return result;
        });

    return vm.count("output")
               ? Writefile(vm["output"].as<std::string>(), results)
               : WriteCout(results);
  }

private:
  static std::optional<std::string>
  Readfile(const std::string &filename) noexcept {
    if (filename == "-") { return Readlines(std::cin); }

    std::ifstream input(filename);

    if (!input) {
      std::cerr << "Failed to open input file: " << filename << std::endl;
      return std::nullopt;
    }

    std::string result = Readlines(input);
    input.close();

    return result;
  }

  static std::optional<std::string> ReadCin() noexcept {
    if (isatty(STDIN_FILENO)) {
      std::cerr << "No input file specified" << std::endl;
      return std::nullopt;
    }

    return Readlines(std::cin);
  }

  static std::string Readlines(std::istream &input) noexcept {
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
  }

  static int Writefile(const std::string &filename,
                       const std::vector<std::vector<std::string>> &results) {
    if (filename == "-") return Writelines(std::cout, results);

    std::ofstream output(filename);

    if (!output) {
      std::cerr << "Failed to open output file: " << filename << std::endl;
      return EXIT_FAILURE;
    }

    Writelines(output, results);

    output.close();

    return EXIT_SUCCESS;
  }

  static int WriteCout(const std::vector<std::vector<std::string>> &results) {
    return Writelines(std::cout, results);
  }

  static int Writelines(std::ostream &os,
                        std::vector<std::vector<std::string>> results) {
    std::vector<std::vector<std::string>>::const_iterator lit =
                                                              results.cbegin(),
                                                          lend =
                                                              results.cend() -
                                                              1;
    os << "complete -c manage.py -n '__fish_complete_suboption test' -a '";

    while (lit != lend) { Writeline(os, lit++, ' '); }
    Writeline(os, lit, "\'\n");

    return EXIT_SUCCESS;
  }

  template <class Sep>
    requires(std::same_as<Sep, char> || std::same_as<Sep, const char *>)
  static void
  Writeline(std::ostream &os,
            std::vector<std::vector<std::string>>::const_iterator lit,
            Sep sep) {
    std::vector<std::string>::const_iterator wit = lit->cbegin(),
                                             wend = lit->cend();
    os << *wit++;
    while (wit != wend) os << '.' << *wit++;
    os << sep;
  }
};

class UpdateAwsOption {
public:
  struct OptionInfo {
    static constexpr const char *name = "update-aws";
    static constexpr const char *description = "Update AWS credentials";
  };

  explicit UpdateAwsOption(const Context &) {}

  ExitStatus Run() {
    std::optional<std::string> home_ek = Env("HOME");

    if (!home_ek) {
      std::cerr << "No HOME environment variable found" << std::endl;
      return EXIT_FAILURE;
    }

    std::filesystem::path home_path = std::filesystem::path(*home_ek),
                          cred_path = home_path /
                                      std::filesystem::path(".aws/credentials");

    if (!std::filesystem::exists(cred_path)) {
      std::cerr << "No AWS credentials file found: " << cred_path << std::endl;
      return EXIT_FAILURE;
    }

    std::ofstream ofs(cred_path, std::ios::trunc);

    if (!ofs) {
      std::cerr << "Failed to open AWS credentials file: " << cred_path
                << std::endl;
      return EXIT_FAILURE;
    }

    const char *aws_access_key_id_ek = "AWS_ACCESS_KEY_ID",
               *aws_secret_access_key_ek = "AWS_SECRET_ACCESS_KEY",
               *aws_session_token_name_ek = "AWS_SESSION_TOKEN";

    std::optional<std::string> aws_access_key_id = Env(aws_access_key_id_ek),
                               aws_secret_access_key =
                                   Env(aws_secret_access_key_ek),
                               aws_session_token =
                                   Env(aws_session_token_name_ek);

    std::vector<const char *> missings;
    missings.reserve(3);

    if (!aws_access_key_id) missings.emplace_back(aws_access_key_id_ek);
    if (!aws_secret_access_key) missings.emplace_back(aws_secret_access_key_ek);
    if (!aws_session_token) missings.emplace_back(aws_session_token_name_ek);

    if (!missings.empty())
      return ShowMissings(missings, "Missing environment variables: ");

    ofs << "[default]\n"
        << "aws_access_key_id = " << *aws_access_key_id << '\n'
        << "aws_secret_access_key = " << *aws_secret_access_key << '\n'
        << "aws_session_token = " << *aws_session_token << std::endl;
    ofs.close();

    std::wcout.imbue(std::locale(Env("LOCALE").value_or("en_US.UTF-8")));
    std::cout << "aws_access_key_id = " << aws_access_key_id->substr(0, 10);
    std::wcout << L"\u2026\n";
    std::cout << "aws_secret_access_key = "
              << aws_secret_access_key->substr(0, 20);
    std::wcout << L"\u2026\n";
    std::cout << "aws_session_token = " << aws_session_token->substr(0, 45);
    std::wcout << L"\u2026\n";
    std::cout << "\033[32mAWS credentials updated\033[0m" << std::endl;

    return EXIT_SUCCESS;
  }
};

template <class Option> class QOption : public OptionSupport<Option> {
public:
  using OptionSupport<Option>::OptionSupport;

  ExitStatus Do(boost::program_options::variables_map &vm) {
    if (!vm.count("query")) {
      std::cerr << "No query string specified" << std::endl;
      return EXIT_FAILURE;
    }

    const std::optional<std::string> host = Env(Option::host_ek),
                                     user = Env(Option::user_ek),
                                     pass = Env(Option::pass_ek),
                                     port = Env(Option::port_ek),
                                     db = Env(Option::db_ek);

    std::vector<const char *> missings;
    missings.reserve(5);

    if (!host) missings.emplace_back(Option::host_ek);
    if (!port) missings.emplace_back(Option::port_ek);
    if (!db) missings.emplace_back(Option::db_ek);
    if (!user) missings.emplace_back(Option::user_ek);
    if (!pass) missings.emplace_back(Option::pass_ek);

    if (!missings.empty())
      return ShowMissings(missings, "Missing environment variables: ");

    return static_cast<Option *>(this)->Execute(
        vm, *host, *user, *pass, *port, *db);
  }
};

template <class Option>
class PqExecOption : public QOption<PqExecOption<Option>> {
public:
  using QOption<PqExecOption>::QOption;

  struct OptionInfo {
    static constexpr const char *name = Option::OptionInfo::name;
    static constexpr const char *description = Option::OptionInfo::description;
  };

  static constexpr const char *host_ek = "PG_HOST", *user_ek = "PG_USR",
                              *pass_ek = "PG_PWD", *port_ek = "PG_PORT",
                              *db_ek = "PG_DB";

  static void AddOptions(boost::program_options::options_description &desc)
    requires requires(Option &o,
                      boost::program_options::options_description &d) {
      { o.AddOptions(d) } -> std::same_as<void>;
    }
  {
    Option::AddOptions(desc);
  }

  static void
  AddPositional(boost::program_options::positional_options_description &p)
    requires requires(
        Option &o, boost::program_options::positional_options_description &q) {
      { o.AddPositional(q) } -> std::same_as<void>;
    }
  {
    Option::AddPositional(p);
  }

  ExitStatus Execute(boost::program_options::variables_map &vm,
                     const std::string &host,
                     const std::string &user,
                     const std::string &pass,
                     const std::string &port,
                     const std::string &db) {
    std::ostringstream oss;
    oss << "host=" << host << " dbname=" << db << " user=" << user
        << " password=" << pass << " port=" << port;

    PGconn *conn = PQconnectdb(oss.str().c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
      std::cerr << "Connection to database failed: " << PQerrorMessage(conn)
                << std::endl;
      PQfinish(conn);
      return EXIT_FAILURE;
    }

    PGresult *res = PQexec(conn, vm["query"].as<std::string>().c_str());

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
      std::cerr << "Query failed: " << PQresultErrorMessage(res) << std::endl;
      PQclear(res);
      PQfinish(conn);
      return EXIT_FAILURE;
    }

    ExitStatus status = static_cast<Option *>(this)->Execute(res);

    PQclear(res);
    PQfinish(conn);

    return status;
  }
};

class PqOption : public PqExecOption<PqOption> {
public:
  struct OptionInfo {
    static constexpr const char *name = "pq";
    static constexpr const char *description = "PostgreSQL query";
  };

  using PqExecOption<PqOption>::PqExecOption;

  static void AddOptions(boost::program_options::options_description &desc) {
    desc.add_options()(
        "query", boost::program_options::value<std::string>(), "SQL Query");
  }

  static void
  AddPositional(boost::program_options::positional_options_description &p) {
    p.add("query", 1);
  }

  ExitStatus Execute(PGresult *res) {
    int rows_count = PQntuples(res);

    if (rows_count) {
      int cols_count = PQnfields(res);

      std::vector<Handler> handlers;
      handlers.reserve(static_cast<std::size_t>(cols_count));

      for (int i = 0; i < cols_count; i++) {
        const char *name = PQfname(res, i);
        Oid oid = PQftype(res, i);
        switch (oid) {
        case 16: handlers.emplace_back(name, OutBool); break;
        case 20:
        case 23:
        case 26: handlers.emplace_back(name, OutAsIs); break;
        case 19:
        case 1043:
        case 1184: handlers.emplace_back(name, OutQuoted); break;
        default: handlers.emplace_back(name, OutUnknown);
        }
      }

      std::cout << "[";
      int i = 0;
      for (; i < rows_count - 1; ++i) {
        WriteAttributes(res, i, cols_count, handlers);
        std::cout << "},";
      }
      WriteAttributes(res, i, cols_count, handlers);
      std::cout << "}]" << std::endl;
    } else std::cout << "No rows" << std::endl;

    return EXIT_SUCCESS;
  }

private:
  struct Handler {
    const char *name;
    void (*out)(PGresult *, int, int);
  };

  static void WriteAttributes(PGresult *res,
                              int i,
                              int cols_count,
                              const std::vector<Handler> &handlers) {
    std::cout << "{\n";
    int j = 0;
    for (; j < cols_count - 1; ++j)
      WriteAttribute(res, i, j, handlers[static_cast<std::size_t>(j)]) << ",\n";
    WriteAttribute(res, i, j, handlers[static_cast<std::size_t>(j)]) << "\n";
  }

  static std::ostream &
  WriteAttribute(PGresult *res, int i, int j, const Handler &handler) {
    std::cout << "  \"" << handler.name << "\": ";
    handler.out(res, i, j);
    return std::cout;
  }

  static void OutBool(PGresult *res, int i, int j) {
    std::cout << (PQgetvalue(res, i, j)[0] == 't' ? "true" : "false");
  }

  static void OutAsIs(PGresult *res, int i, int j) {
    std::cout << PQgetvalue(res, i, j);
  }

  static void OutQuoted(PGresult *res, int i, int j) {
    std::cout << '"' << PQgetvalue(res, i, j) << '"';
  }

  static void OutUnknown(PGresult *res, int i, int j) {
    std::cout << "\"[" << PQgetvalue(res, i, j) << " (" << PQftype(res, j)
              << ")]\"";
  }
};

class PqAgentsOption : public PqExecOption<PqAgentsOption> {
public:
  struct OptionInfo {
    static constexpr const char *name = "pq-agents";
    static constexpr const char *description = "PostgreSQL agents query";
  };

  using PqExecOption<PqAgentsOption>::PqExecOption;

  ExitStatus Execute(PGresult *res) {
    int rows_count = PQntuples(res);

    if (rows_count) {
      int cols_count = PQnfields(res);
      std::cout << cols_count << std::endl;

    } else std::cout << "No rows" << std::endl;

    return EXIT_SUCCESS;
  }
};

class MqOption : public QOption<MqOption> {
public:
  struct OptionInfo {
    static constexpr const char *name = "mq";
    static constexpr const char *description = "MySQL query";
  };

  using QOption<MqOption>::QOption;

  static constexpr const char *host_ek = "MYSQL_HOST", *user_ek = "MYSQL_USR",
                              *pass_ek = "MYSQL_PWD", *port_ek = "MYSQL_PORT",
                              *db_ek = "MYSQL_DB";

  static void AddOptions(boost::program_options::options_description &desc) {
    desc.add_options()(
        "query", boost::program_options::value<std::string>(), "SQL Query");
  }

  static void
  AddPositional(boost::program_options::positional_options_description &p) {
    p.add("query", 1);
  }

  ExitStatus Execute(boost::program_options::variables_map &vm,
                     const std::string &host,
                     const std::string &user,
                     const std::string &pass,
                     const std::string &port,
                     const std::string &db) {

    MYSQL *conn = mysql_init(nullptr);
    if (!mysql_real_connect(conn,
                            host.c_str(),
                            user.c_str(),
                            pass.c_str(),
                            db.c_str(),
                            static_cast<unsigned int>(std::stoi(port)),
                            nullptr,
                            0)) {
      std::cerr << "Connection to database failed: " << mysql_error(conn)
                << std::endl;
      return EXIT_FAILURE;
    }

    if (mysql_query(conn, vm["query"].as<std::string>().c_str())) {
      std::cerr << "Query failed: " << mysql_error(conn) << std::endl;
      return EXIT_FAILURE;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
      std::cerr << "Failed to store result: " << mysql_error(conn) << std::endl;
      return EXIT_FAILURE;
    }

    MYSQL_ROW row = mysql_fetch_row(res);

    if (row) {

      MYSQL_FIELD *fields = mysql_fetch_fields(res);
      unsigned int num_fields = mysql_num_fields(res);

      std::vector<Handler> handlers;
      handlers.reserve(static_cast<std::size_t>(num_fields));

      for (unsigned int i = 0; i < num_fields; i++) {
        const char *name = fields[i].name;
        switch (fields[i].type) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG: handlers.emplace_back(name, OutAsIs); break;
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_STRING: handlers.emplace_back(name, OutQuoted); break;
        case MYSQL_TYPE_BOOL: handlers.emplace_back(name, OutBool); break;
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_NULL:
        case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_NEWDATE:
        case MYSQL_TYPE_BIT:
        case MYSQL_TYPE_TIMESTAMP2:
        case MYSQL_TYPE_DATETIME2:
        case MYSQL_TYPE_TIME2:
        case MYSQL_TYPE_TYPED_ARRAY:
        case MYSQL_TYPE_INVALID:
        case MYSQL_TYPE_JSON:
        case MYSQL_TYPE_NEWDECIMAL:
        case MYSQL_TYPE_ENUM:
        case MYSQL_TYPE_SET:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_GEOMETRY:
          handlers.emplace_back(name, OutUnknown);
          std::cout << " (";
          break;
        default: handlers.emplace_back(name, OutUnknown);
        }
      }

      std::cout << "[";
      for (;;) {
        std::cout << "{\n";
        unsigned long *lengths = mysql_fetch_lengths(res);
        for (unsigned int i = 0; i < num_fields; ++i) {
          std::cout << "  \"" << handlers[i].name << "\": ";
          handlers[i].out(row, lengths, i, fields);
          std::cout << ",\n";
        }
        row = mysql_fetch_row(res);
        if (row) std::cout << "},";
        else {
          std::cout << "}";
          break;
        }
      }
      std::cout << "]" << std::endl;
    } else std::cout << "No rows" << std::endl;

    mysql_free_result(res);
    mysql_close(conn);

    return EXIT_SUCCESS;
  }

private:
  struct Handler {
    const char *name;
    void (*out)(MYSQL_ROW, unsigned long *, unsigned int, MYSQL_FIELD *);
  };

  static void
  OutBool(MYSQL_ROW row, unsigned long *, unsigned int i, MYSQL_FIELD *) {
    std::cout << (row[i][0] == '1' ? "true" : "false");
  }

  static void OutAsIs(MYSQL_ROW row,
                      unsigned long *lengths,
                      unsigned int i,
                      MYSQL_FIELD *) {
    std::cout.write(row[i], static_cast<std::streamsize>(lengths[i]));
  }

  static void OutQuoted(MYSQL_ROW row,
                        unsigned long *lengths,
                        unsigned int i,
                        MYSQL_FIELD *) {
    std::cout << '"';
    std::cout.write(row[i], static_cast<std::streamsize>(lengths[i]));
    std::cout << '"';
  }

  static void OutUnknown(MYSQL_ROW row,
                         unsigned long *lengths,
                         unsigned int i,
                         MYSQL_FIELD *fields) {
    std::cout << "[";
    std::cout.write(row[i], static_cast<std::streamsize>(lengths[i]));
    std::cout << " (" << fields[i].type << ")]";
  }
};

template <class... Option> struct Booking;

template <class Option> struct Booking<Option> {
  static void Book(const Context &ctx) {
    return Dispatcher<Option>::Dispatch(ctx, ctx.argv[1]);
  }
};

class NpqAgentsOption : public OptionSupport<NpqAgentsOption> {
public:
  struct OptionInfo {
    static constexpr const char *name = "agents";
    static constexpr const char *description = "Pq agents query";
  };

  static constexpr const char *host_ek = "PG_HOST", *user_ek = "PG_USR",
                              *pass_ek = "PG_PWD", *port_ek = "PG_PORT",
                              *db_ek = "PG_DB";

  using OptionSupport<NpqAgentsOption>::OptionSupport;

  static void AddOptions(boost::program_options::options_description &desc) {
    desc.add_options()("sector,s",
                       boost::program_options::value<std::string>(),
                       "Sector label");
  }

  ExitStatus Do(boost::program_options::variables_map &vm) {
    const std::optional<std::string> host = Env(host_ek), user = Env(user_ek),
                                     pass = Env(pass_ek), port = Env(port_ek),
                                     db = Env(db_ek);

    std::vector<const char *> missings;
    missings.reserve(5);

    if (!host) missings.emplace_back(host_ek);
    if (!port) missings.emplace_back(port_ek);
    if (!db) missings.emplace_back(db_ek);
    if (!user) missings.emplace_back(user_ek);
    if (!pass) missings.emplace_back(pass_ek);

    if (!missings.empty())
      return ShowMissings(missings, "Missing environment variables: ");

    std::ostringstream oss;
    oss << "host=" << *host << " dbname=" << *db << " user=" << *user
        << " password=" << *pass << " port=" << *port;

    PGconn *conn = PQconnectdb(oss.str().c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
      std::cerr << "Connection to database failed: " << PQerrorMessage(conn)
                << std::endl;
      PQfinish(conn);
      return EXIT_FAILURE;
    }

    if (!vm.count("sector")) {}

    PGresult *res =
        PQexec(conn, "SELECT * FROM assignment_demand_agent LIMIT 200");

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
      std::cerr << "Query failed: " << PQresultErrorMessage(res) << std::endl;
      PQclear(res);
      PQfinish(conn);
      return EXIT_FAILURE;
    }

    int rows_count = PQntuples(res);
    int cols_count = PQnfields(res);

    for (int i = 0; i < cols_count; i++) {
      std::cout << PQfname(res, i) << '\t';
    }
    std::cout << std::endl;

    for (int i = 0; i < rows_count; i++) {
      for (int j = 0; j < cols_count; j++) {
        std::cout << PQgetvalue(res, i, j) << '\t';
      }
      std::cout << std::endl;
    }

    PQclear(res);
    PQfinish(conn);

    return EXIT_SUCCESS;
  }
};

class NpqRecordsOption {
public:
  struct OptionInfo {
    static constexpr const char *name = "npq-records";
    static constexpr const char *description = "N PostgreSQL records query";
  };

  explicit NpqRecordsOption(const Context &) {}

  ExitStatus Run() {
    std::cerr << "No query string specified" << std::endl;
    return EXIT_FAILURE;
  }
};

class NpqOption : public OptionSupport<NpqOption> {
public:
  struct OptionInfo {
    static constexpr const char *name = "npq";
    static constexpr const char *description = "N PostgreSQL query";
  };

  using Options = OptionLs<NpqAgentsOption, NpqRecordsOption>;
  using OptionSupport<NpqOption>::OptionSupport;

  static void AddOptions(boost::program_options::options_description &desc) {
    desc.add_options()("option",
                       boost::program_options::value<std::string>(),
                       "Npq option")("help,h", "Show help");
  }

  static void
  AddPositional(boost::program_options::positional_options_description &p) {
    p.add("option", 1);
  }

  ExitStatus Do(boost::program_options::variables_map &vm) {
    if (!vm.count("option") or vm.count("help")) {
      std::cout << "Usage: " << ctx.argv[0] << "npq [option]\n";
      Info<Options>::Show();
      return EXIT_FAILURE;
    }

    return Dispatcher<Options>::Dispatch(
        ctx, vm["option"].as<std::string>().c_str());
  }
};

using Options = OptionLs<class DjTestNamesOption,
                         class UpdateAwsOption,
                         class PqOption,
                         class MqOption,
                         class NpqOption,
                         class HelpOption,
                         class CompleteOption>;

class CompleteOption {
public:
  struct OptionInfo {
    static constexpr const char *name = "complete";
    static constexpr const char *description = "Show completion script";
  };

  explicit CompleteOption(const Context &ctx) : cmd{ctx.argv[0]} {}

  ExitStatus Run() {
    std::cout << "complete -c '" << cmd
              << "' -e -n '__fish_use_subcommand'\ncomplete -c '" << cmd
              << "' -f\n";
    Completer<Options>::Definition(cmd);
    std::cout << std::endl;
    return EXIT_SUCCESS;
  }

private:
  const char *cmd;

  template <class... Options> struct Completer;
  template <class... Options> struct Completer<OptionLs<Options...>> {
    static void Definition(const char *cmd) {
      ((std::cout << "complete -c " << cmd << " -n '__fish_use_subcommand' -a '"
                  << Options::OptionInfo::name << "' -d '"
                  << Options::OptionInfo::description << "'\n"),
       ...);
    }
  };
};

class HelpOption {
public:
  struct OptionInfo {
    static constexpr const char *name = "help";
    static constexpr const char *description = "Show help";
  };

  explicit HelpOption(const Context &c) : ctx{c} {}

  ExitStatus Run() {
    std::cout << "Usage: " << ctx.argv[0] << " [option]\n";
    Info<Options>::Show();
    return EXIT_SUCCESS;
  }

private:
  const Context &ctx;

  static int Main(int argc, const char *argv[]) {
    Context ctx{argc, argv};

    if (argc == 1) {
      HelpOption(ctx).Run();
      return EXIT_FAILURE;
    }

    return Dispatcher<Options>::Dispatch(ctx, argv[1]);
  }
};

} // namespace option_sp

class OptionSupport {};

class DjTestNamesOption : public OptionSupport {};

int main(int argc, const char *argv[]) {
  boost::program_options::options_description desc{"s2sak"};
  desc.add_options()("help,h", "Show help")(
      "cmd", boost::program_options::value<std::string>(), "To execute");

  boost::program_options::positional_options_description p;
  p.add("cmd", 1);

  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::command_line_parser(argc, argv)
          .options(desc)
          .positional(p)
          .allow_unregistered()
          .run(),
      vm);
  boost::program_options::notify(vm);

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    return EXIT_SUCCESS;
  }

  if (vm.count("cmd")) {
    std::cout << "cmd: " << vm["cmd"].as<std::string>() << std::endl;
  }

  return EXIT_SUCCESS;
}
