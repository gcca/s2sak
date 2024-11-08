#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include <boost/program_options.hpp>

using ExitStatus = int;

class Context {
public:
  boost::program_options::options_description opts;
  boost::program_options::variables_map vm;
  int argc;
  const char **argv;
};

template <class OptionC> class OptionSupport {
public:
  OptionSupport(const Context &ctx) : ctx{ctx}, opts{OptionC::name} {
    if constexpr (requires { static_cast<OptionC *>(this)->Init(); })
      static_cast<OptionC *>(this)->Init();

    opts.add_options()("help", "help message");

    const std::vector<std::string> args(ctx.argv + 2, ctx.argv + ctx.argc);
    boost::program_options::store(
        boost::program_options::command_line_parser(args).options(opts).run(),
        vm);
    boost::program_options::notify(vm);
  }

  ExitStatus Run() {
    if (vm.count("help")) {
      std::cout << ctx.argv[0] << ' ' << opts << std::endl;
      return EXIT_SUCCESS;
    }

    return static_cast<OptionC *>(this)->Do();
  }

protected:
  const Context &ctx;
  boost::program_options::options_description opts;
  boost::program_options::variables_map vm;
};

class DjTestNamesOption : public OptionSupport<DjTestNamesOption> {
public:
  static constexpr const char *name = "dj-test-names";
  using OptionSupport<DjTestNamesOption>::OptionSupport;

  void Init() {
    opts.add_options() //
        ("input,i", boost::program_options::value<std::string>(),
         "input file") //
        ("output,o", boost::program_options::value<std::string>(),
         "output file");
  }

  ExitStatus Do() {
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
        std::sregex_iterator{}, std::back_inserter(results),
        [&word_p](const std::smatch &match) {
          std::vector<std::string> result;
          result.reserve(10);

          const std::string smatch = match[2];
          std::transform(
              std::sregex_iterator{smatch.cbegin(), smatch.cend(), word_p},
              std::sregex_iterator{}, std::back_inserter(result),
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

class UpdateAwsOption : public OptionSupport<UpdateAwsOption> {
public:
  static constexpr const char *name = "update-aws";
  using OptionSupport<UpdateAwsOption>::OptionSupport;

  ExitStatus Do() {
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

    std::vector<std::string> missings;
    missings.reserve(3);

    if (!aws_access_key_id) missings.emplace_back(aws_access_key_id_ek);
    if (!aws_secret_access_key) missings.emplace_back(aws_secret_access_key_ek);
    if (!aws_session_token) missings.emplace_back(aws_session_token_name_ek);

    if (!missings.empty()) {
      std::cerr << "Missing environment variables: ";
      std::copy(missings.cbegin(), missings.cend() - 1,
                std::ostream_iterator<std::string>(std::cerr, ", "));
      std::cerr << missings.back() << std::endl;
      return EXIT_FAILURE;
    }

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

private:
  static std::optional<std::string> Env(const char *key) {
    if (const char *raw = std::getenv(key)) {
      std::string value(raw);

      value.erase(value.begin(),
                  std::find_if(value.begin(), value.end(), [](unsigned char c) {
                    return !std::isspace(c);
                  }));
      value.erase(std::find_if(value.rbegin(), value.rend(),
                               [](unsigned char c) { return !std::isspace(c); })
                      .base(),
                  value.end());

      return value;
    }
    return std::nullopt;
  }
};

class CompleteOption {
public:
  static constexpr const char *name = "complete";
  explicit CompleteOption(const Context &) {}

  ExitStatus Run() {
    std::cout
        << "complete -c s2sak -e -n '__fish_use_subcommand'\n"
           "complete -c s2sak -f\n"
           "complete -c s2sak -n \"__fish_use_subcommand\" -a dj-test-names -d "
           "\"Dj test names complete script\"\n"
           "complete -c s2sak -n \"__fish_use_subcommand\" -a update-aws -d "
           "\"Update AWS credentials\"\n"
           "complete -c s2sak -n \"__fish_use_subcommand\" -a help -d \"Show "
           "help\""
        << std::endl;

    return EXIT_SUCCESS;
  }
};

template <class... Ls> struct OptionLs;
template <class... Options> struct Dispatcher {
  template <class Option> static ExitStatus Dispatch(const Context &ctx) {
    Option cmd(ctx);
    return cmd.Run();
  }
};

template <class Option> struct Dispatcher<OptionLs<Option>> {
  static ExitStatus Dispatch(const Context &ctx, const std::string &name) {
    if (std::string_view(name) == Option::name) {
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
    if (name == Option::name) {
      return Dispatcher<>::Dispatch<Option>(ctx);
    } else {
      return Dispatcher<OptionLs<Options...>>::Dispatch(ctx, name);
    }
  }
};

template <class... Options> struct ShowOptions;
template <class... Options> struct ShowOptions<OptionLs<Options...>> {
  static void Show() {
    ((std::cout << "\n    " << Options::name), ...);
    std::cout << std::endl;
  }
};

using Options = OptionLs<    //
    class DjTestNamesOption, //
    class UpdateAwsOption,   //
    class HelpOption,        //
    class CompleteOption     //
    >;

class HelpOption {
public:
  static constexpr const char *name = "help";
  explicit HelpOption(const Context &ctx) : ctx{ctx} {}

  ExitStatus Run() {
    std::cout << ctx.opts;
    ShowOptions<Options>::Show();
    return EXIT_SUCCESS;
  }

private:
  const Context &ctx;
};

int main(int argc, const char *argv[]) {
  Context ctx{{"s2sak"}, {}, argc, argv};

  if (argc == 1) {
    std::cerr << "No option specified: " << argv[0] << " [option]\n";
    ShowOptions<Options>::Show();
    return EXIT_FAILURE;
  }

  ctx.opts.add_options()("option", boost::program_options::value<std::string>(),
                         "option");

  boost::program_options::positional_options_description p;
  p.add("option", 1);

  boost::program_options::store(
      boost::program_options::command_line_parser(2, argv)
          .options(ctx.opts)
          .positional(p)
          .allow_unregistered()
          .run(),
      ctx.vm);
  boost::program_options::notify(ctx.vm);

  return Dispatcher<Options>::Dispatch(ctx, ctx.vm["option"].as<std::string>());
}
