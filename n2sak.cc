#include <iostream>

#include <boost/beast/core.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <boost/program_options.hpp>

typedef int ExitStatus;

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

template <class Option, class Suboptions> class With {
public:
  explicit With(const Context &c) : ctx{c} {}

  ExitStatus Run() {
    Option &option = *static_cast<Option *>(this);
    if constexpr (requires(boost::program_options::variables_map &vm,
                           boost::program_options::parsed_options &parsed) {
                    { option.Do(vm, parsed) } -> std::same_as<ExitStatus>;
                  }) {

      boost::program_options::options_description desc{
          Option::OptionInfo::name};
      desc.add_options()("help,h", "Show help");

      Option::AddOptions(desc);

      std::cout << ">>> argc = " << ctx.argc << std::endl;
      for (int i = 0; i < ctx.argc; ++i) {
        std::cout << ">>> argv[" << i << "] = " << ctx.argv[i] << std::endl;
      }

      boost::program_options::command_line_parser parser{ctx.argc - 1,
                                                         ctx.argv + 1};
      parser.options(desc);

      if constexpr (requires(
                        boost::program_options::positional_options_description
                            &p) { option.AddPositional(p); }) {
        boost::program_options::positional_options_description p;
        option.AddPositional(p);
        parser.positional(p);
      }

      parser.allow_unregistered();

      boost::program_options::parsed_options parsed = parser.run();

      boost::program_options::variables_map vm;
      try {
        boost::program_options::store(parsed, vm);
      } catch (const boost::program_options::error &e) {
        std::cerr << "ERROR(store): " << e.what() << std::endl;
        return EXIT_FAILURE;
      }

      try {
        boost::program_options::notify(vm);
      } catch (const boost::program_options::error &e) {
        std::cerr << "ERROR(notify): " << e.what() << std::endl;
        return EXIT_FAILURE;
      }

      if (vm.count("help")) {
        std::cout << "Usage: " << ctx.argv[0] << " "
                  << Option::OptionInfo::name;
        Info<Suboptions>::Show();
        std::cout << desc << std::endl;
        return EXIT_SUCCESS;
      }

      // ……
      std::vector<std::string> unrecognized =
          boost::program_options::collect_unrecognized(
              parsed.options, boost::program_options::include_positional);

      if (!unrecognized.empty()) {
        std::cerr << "Unrecognized options: ";
        for (const auto &opt : unrecognized) { std::cerr << opt << " "; }
        std::cerr << std::endl;
        return EXIT_FAILURE;
      }
      // ……

      return option.Do(vm, parsed);
    } else if constexpr (requires {
                           { option.Do() } -> std::same_as<ExitStatus>;
                         }) {
      return option.Do();
    }

    return EXIT_SUCCESS;
  }

  Context ctx;
};

using Options =
    OptionLs<class FooOption, class HelpOption, class CompleteOption>;

class BarOption {
public:
  struct OptionInfo {
    static constexpr const char *name = "bar";
    static constexpr const char *description = "Bar option";
  };

  ExitStatus Run() {
    std::cout << "Bar option" << std::endl;
    return EXIT_SUCCESS;
  }
};

class BazOption {
public:
  struct OptionInfo {
    static constexpr const char *name = "baz";
    static constexpr const char *description = "Baz option";
  };

  ExitStatus Run() {
    std::cout << "Baz option" << std::endl;
    return EXIT_SUCCESS;
  }
};

using FooSuboptions = OptionLs<BarOption, BazOption>;

class FooOption : public With<FooOption, FooSuboptions> {
public:
  struct OptionInfo {
    static constexpr const char *name = "foo";
    static constexpr const char *description = "Foo option";
  };

  using With<FooOption, FooSuboptions>::With;

  static void AddOptions(boost::program_options::options_description &desc) {
    desc.add_options()(
        "name,n", boost::program_options::value<std::string>(), "Raw");
  }

  ExitStatus Do(boost::program_options::variables_map &,
                boost::program_options::parsed_options &) {
    std::cout << "Foo option" << std::endl;

    // Dispatcher<FooSubs>::Dispatch(ctx, ctx.argv[2]);
    return EXIT_SUCCESS;
  }
};

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

int main(int argc, const char *argv[]) {
  if (argc < 2) { return HelpOption{Context{argc, argv}}.Run(); }

  return Dispatcher<Options>::Dispatch(Context{argc, argv}, argv[1]);
}
