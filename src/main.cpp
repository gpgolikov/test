#include <iostream>
#include <string_view>
#include <vector>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "multi_queue_processor.h"
#include "consumers.h"

namespace po = boost::program_options;
namespace fs = boost::filesystem;

using namespace std::literals;

namespace griha
{

namespace
{

/// @name Free functions to process command line options
/// @{

void usage(const char* argv0, std::ostream& os, const po::options_description& opts_desc)
{
    os << "Usage:" << std::endl
       << '\t' << fs::path{ argv0 }.stem().string() << " [<threads-number>]" << std::endl
       << '\t' << opts_desc << std::endl;
}

/// @}

} // unnamed namespace

constexpr auto c_text = 
    "Strange he never saw his real country. Ireland my country. Member for College green. He boomed that workaday worker tack "
    "for all it was worth. It’s the ads and side features sell a weekly, not the stale news in the official gazette. Queen Anne "
    "is dead. Published by authority in the year one thousand and. Demesne situate in the townland of Rosenallis, barony of "
    "Tinnahinch. To all whom it may concern schedule pursuant to statute showing return of number of mules and jennets exported "
    "from Ballina. Nature notes. Cartoons. Phil Blake’s weekly Pat and Bull story. Uncle Toby’s page for tiny tots. Country bumpkin’s "
    "queries. Dear Mr Editor, what is a good cure for flatulence? I’d like that part. Learn a lot teaching others. The personal note. "
    "M. A. P. Mainly all pictures. Shapely bathers on golden strand. World’s biggest balloon. Double marriage of sisters celebrated. "
    "Two bridegrooms laughing heartily at each other. Cuprani too, printer. More Irish than the Irish."sv;

constexpr auto c_delim = " ,.!?;\"'-\n\t"sv;

auto split(std::string_view value, std::string_view delim)
{
    std::vector<std::string_view> res;

    while (!value.empty())
    {
        auto pos = value.find_first_of(delim);
        res.push_back(value.substr(0, pos));
        pos = value.find_first_not_of(delim, pos);
        if (pos == std::string_view::npos)
            break;
        value.remove_prefix(pos);
    }

    return res;
}

constexpr auto EVEN_SIZE_WORD_QUEUE = 1;
constexpr auto ODD_SIZE_WORD_QUEUE = 2;

void process(std::string_view text, test::MultiQueueProcessor<std::string>& queues)
{
    const auto words = split(text, c_delim);
    for (const auto& word : words)
    {
        queues.Enqueue(word.size() % 2 == 0 ? EVEN_SIZE_WORD_QUEUE : ODD_SIZE_WORD_QUEUE, word);
    }
}

} // namespace griha

int main(int argc, char* argv[])
{
    using namespace griha;

    bool opt_help;
    int nthreads = -1;

    // command line options
    po::options_description generic { "Options" };
    generic.add_options()
            ("help,h", po::bool_switch(&opt_help), "prints out this message");

    // Next options allowed at command line, but isn't shown in help
    po::options_description hidden {};
    hidden.add_options()("threads-number", po::value(&nthreads));
    po::positional_options_description pos;
    pos.add("threads-number", -1);

    po::options_description cmd_line, visible;
    cmd_line.add(generic).add(hidden);
    visible.add(generic);

    po::variables_map opts;
    try
    {
        po::store(po::command_line_parser(argc, argv).options(cmd_line).positional(pos).run(), opts);
        notify(opts);
    }
    catch (...)
    {
        usage(argv[0], std::cerr, visible);
        return EXIT_FAILURE;
    }

    if (opt_help)
    {
        usage(argv[0], std::cout, visible);
        return EXIT_SUCCESS;
    }

    if (nthreads <= 0)
    {
        usage(argv[0], std::cerr, visible);
        return EXIT_SUCCESS;
    }

    test::MultiQueueProcessor<std::string> queues;
    
    unsigned odd_count = 0, even_count = 0;
    const auto consumers = test::MakeConsumers<std::string, EVEN_SIZE_WORD_QUEUE>(
        [&even_count] (unsigned, const std::string& value)
        {
            ++even_count;
        },
        [&odd_count] (unsigned, const std::string& value)
        {
            ++odd_count;
        });
    consumers.SubscribeTo(queues);

    queues.Start();

    auto text = c_text;
    const auto length = text.size() / nthreads;

    std::vector<std::thread> threads;
    threads.reserve(nthreads - 1);

    while (threads.size() < nthreads - 1)
    {
        const auto pos = text.find_first_of(c_delim, length);

        threads.emplace_back([value = text.substr(0, pos), &queues]
        {
            process(value, queues);
        });
        text.remove_prefix(pos);
    }

    process(text, queues);

    for (auto& th : threads)
    {
        th.join();
    }

    queues.Stop();

    std::cout << "Even word sizes count = " << even_count << std::endl;
    std::cout << "Odd word sizes count = " << odd_count << std::endl;

    return EXIT_SUCCESS;
}
