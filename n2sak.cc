#include <iostream>

#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <boost/program_options.hpp>

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

static void Usage(int argc, char *argv[]) {
  std::cerr << "Usage: " << argv[0] << " <" << argc << ">" << std::endl;
}

using Options = OptionLs<class HelpOption, class CompleteOption>;

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
};

int main(int argc, char *argv[]) {
  if (argc <= 2) {
    Usage(argc, argv);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
