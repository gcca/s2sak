#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <istream>
#include <regex>
#include <vector>

#include <boost/program_options.hpp>

class DjTestNamesCommand {
public:
  explicit DjTestNamesCommand(const std::vector<std::string> &args)
      : opts("dj-test-names") {
    opts.add_options() //
        ("input,i", boost::program_options::value<std::string>(),
         "input file") //
        ("output,o", boost::program_options::value<std::string>(),
         "output file") //
        ("help", "produce help message");

    boost::program_options::store(
        boost::program_options::command_line_parser(args).options(opts).run(),
        vm);
    boost::program_options::notify(vm);
  }

  int Run() {
    if (vm.count("help")) {
      std::cout << opts << std::endl;
      return EXIT_SUCCESS;
    }

    std::string input_str = vm.count("input")
                                ? Readfile(vm["input"].as<std::string>())
                                : Readlines(std::cin);

    std::vector<std::vector<std::string>> results;
    results.reserve(80);

    std::regex line_p(R"((test_\w+) \((\w+(?:\.\w+)+)\))");
    std::regex word_p(R"((\w+))");

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
               : Writelines(std::cout, results);
  }

private:
  boost::program_options::options_description opts;
  boost::program_options::variables_map vm;

  static std::string Readfile(const std::string &filename) noexcept {
    if (filename == "-") { return Readlines(std::cin); }

    std::ifstream input(filename);

    if (!input) {
      std::cerr << "Failed to open input file: " << filename << std::endl;
      return "";
    }

    std::string result = Readlines(input);
    input.close();

    return result;
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

  static int Writelines(std::ostream &os,
                        std::vector<std::vector<std::string>> results) {
    std::vector<std::vector<std::string>>::const_iterator lit =
                                                              results.cbegin(),
                                                          lend =
                                                              results.cend() -
                                                              1;
    os << "complete -c manage.py -n '__fish_complete_subcommand test' -a '";

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

int main(int argc, char *argv[]) {
  boost::program_options::options_description opts("s2sak");
  opts.add_options()                   //
      ("help", "produce help message") //
      ("command", boost::program_options::value<std::string>(), "command");

  boost::program_options::positional_options_description p;
  p.add("command", 1);

  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::command_line_parser(2, argv)
          .options(opts)
          .positional(p)
          .allow_unregistered()
          .run(),
      vm);
  boost::program_options::notify(vm);

  if (vm.count("command")) {
    const std::string command = vm["command"].as<std::string>();
    const std::vector<std::string> args(argv + 2, argv + argc);

    if (command == "dj-test-names") {
      DjTestNamesCommand cmd(args);
      return cmd.Run();
    } else {
      std::cerr << "Unknown command: " << command << std::endl;
      return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
  }

  if (vm.count("help")) {
    std::cout << opts << std::endl;
    return EXIT_SUCCESS;
  }

  std::cerr << "No command specified" << std::endl;
  return EXIT_FAILURE;
}
