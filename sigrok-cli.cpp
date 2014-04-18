/*
 * This file is part of the sigrok-cli-c++ project.
 * 
 * Copyright (C) 2013 Martin Ling <martin-sigrok@earth.li>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libsigrok/libsigrok.hpp>
#include "cpp-optparse/OptionParser.h"

using namespace std;
using namespace sigrok;
using optparse::OptionParser;

const char *VERSION = "0.1";

/* Helper function to print a one-line description of a device. */
void print_device_info(shared_ptr<HardwareDevice> device)
{
    printf("%s -", device->get_driver()->get_name().c_str());
    vector<string> parts =
        {device->get_vendor(), device->get_model(), device->get_version()};
    for (string part : parts)
        if (part.length() > 0)
            printf(" %s", part.c_str());
    auto channels = device->get_channels();
    printf(" with %zd channels:", channels.size());
    for (auto channel : channels)
        printf(" %s", channel->get_name().c_str());
    printf("\n");
}

/* Helper function to split a string on a delimiter character. */
list<string> split(const string s, char delim)
{
    stringstream ss(s);
    string item;
    list<string> elems;
    while (getline(ss, item, delim))
        elems.push_back(item);
    return elems;
}

/* Callable object for SIGINT handling. */
function<void()> sigint_handler;

/* C signal handler shim. */
void sigint(int signum)
{
    sigint_handler();
}

int main(int argc, char *argv[])
{
    OptionParser parser = OptionParser();

    /* Set up command line options. */
    parser.add_option("-V", "--version").help("Show version").action("store_true");
    parser.add_option("-l", "--loglevel").help("Set log level").type("int");
    parser.add_option("-d", "--driver").help("The driver to use");
    parser.add_option("-c", "--config").help("Specify device configuration options");
    parser.add_option("-i", "--input-file").help("Load input from file").dest("input_file");
    parser.add_option("-I", "--input-format").help("Input format");
    parser.add_option("-O", "--output-format").help("Output format").set_default("bits");
    parser.add_option("-p", "--channels").help("Channels to use");
    parser.add_option("-g", "--channel-group").help("Channel group to use");
    parser.add_option("--scan").help("Scan for devices").action("store_true");
    parser.add_option("--time").help("How long to sample (ms)");
    parser.add_option("--samples").help("Number of samples to acquire");
    parser.add_option("--frames").help("Number of frames to acquire");
    parser.add_option("--continuous").help("Sample continuously").action("store_true");
    parser.add_option("--set").help("Set device options only").action("store_true");

    /* Parse command line options. */
    optparse::Values args = parser.parse_args(argc, argv);

    /* Check for valid combination of arguments to proceed. */
    if (!(args.is_set("version")
        || args.is_set("scan")
        || (args.is_set("driver") && (
            args.is_set("set") ||
            args.is_set("time") ||
            args.is_set("samples") ||
            args.is_set("frames") ||
            args.is_set("continuous")))
        || args.is_set("input_file")))
    {
        parser.print_help();
        return 1;
    }

    auto context = Context::create();

    if (args.is_set("version"))
    {
        /* Display version information. */
        printf("%s %s", argv[0], VERSION);
        printf("\nUsing libsigrok %s (lib version %s).",
            context->get_package_version().c_str(),
            context->get_lib_version().c_str());
        printf("\nSupported hardware drivers:\n");
        for (auto entry : context->get_drivers())
        {
            auto driver = entry.second;
            printf("  %-20s %s\n",
                driver->get_name().c_str(),
                driver->get_longname().c_str());
        }
        printf("\nSupported input formats:\n");
        for (auto entry : context->get_input_formats())
        {
            auto input = entry.second;
            printf("  %-20s %s\n",
                input->get_name().c_str(),
                input->get_description().c_str());
        }
        printf("\nSupported output formats:\n");
        for (auto entry : context->get_output_formats())
        {
            auto output = entry.second;
            printf("  %-20s %s\n",
                output->get_name().c_str(),
                output->get_description().c_str());
        }
        printf("\n");
        return 0;
    }

    if (args.is_set("loglevel"))
        context->set_loglevel(LogLevel::get(stoi(args["loglevel"])));

    if (args.is_set("scan") && !args.is_set("driver"))
    {
        /* Scan for devices using all drivers. */
        for (auto entry : context->get_drivers())
        {
            auto driver = entry.second;
            for (auto device : driver->scan({}))
                print_device_info(device);
        }
        return 0;
    }

    shared_ptr<Device> device;
    shared_ptr<HardwareDevice> hwdevice;
    shared_ptr<InputFileDevice> ifdevice;

    if (args.is_set("input_file"))
    {
        /* Load data from a file. */
        shared_ptr<InputFormat> format;
        auto formats = context->get_input_formats();

        if (args.is_set("input_format"))
        {
            /* Use specified input format. */
            format = formats[args["input_format"]];
        }
        else
        {
            /* Find first input format that matches data. */
            bool matched = false;
            for (auto entry : formats)
            {
                format = entry.second;
                if (format->format_match(args["input_file"]))
                {
                    matched = true;
                    break;
                }
            }
            if (!matched)
            {
                printf("File not in any recognised input format.\n");
                return 1;
            }
        }

        /* Open virtual device for input file. */
        ifdevice = format->open_file(args["input_file"], {});
        device = ifdevice;
    }
    else if (args.is_set("driver"))
    {
        /* Separate driver name and configuration options. */
        auto driver_spec = split(args["driver"], ':');

        /* Use specified driver. */
        auto driver = context->get_drivers()[driver_spec.front()];
        driver_spec.pop_front();

        /* Parse key=value configuration pairs. */
        map<const ConfigKey *, Glib::VariantBase> scan_options;
        for (auto pair : driver_spec)
        {
            auto parts = split(pair, '=');
            auto name = parts.front(), value = parts.back();
            auto key = ConfigKey::get(name);
            scan_options[key] = key->parse_string(value);
        }

        /* Scan for devices. */
        auto devices = driver->scan(scan_options);

        if (args.is_set("scan"))
        {
            /* Scan requested only. */
            for (auto device : devices)
                print_device_info(device);
            return 0;
        }

        /* Use first device found. */
        hwdevice = devices.front();
        hwdevice->open();
        device = hwdevice;

        /* Apply device settings from command line. */
        if (args.is_set("time"))
            hwdevice->config_set(ConfigKey::LIMIT_MSEC, args["time"]);
        if (args.is_set("samples"))
            hwdevice->config_set(ConfigKey::LIMIT_SAMPLES, args["samples"]);
        if (args.is_set("frames"))
            hwdevice->config_set(ConfigKey::LIMIT_FRAMES, args["frames"]);

        if (args.is_set("config"))
        {
            /* Split into key=value pairs. */
            auto pairs = split(args["config"], ':');

            /* Parse and apply key=value configuration pairs. */
            for (auto pair : pairs)
            {
                auto parts = split(pair, '=');
                auto name = parts.front(), value = parts.back();
                auto key = ConfigKey::get(name);
                hwdevice->config_set(key, value);
            }
        }
    }

    if (args.is_set("channels"))
    {
        /* Enable selected channels only. */
        auto enabled = split(args["channels"], ',');
        auto enabled_set = unordered_set<string>(enabled.begin(), enabled.end());
        for (auto channel : device->get_channels())
            channel->set_enabled(enabled_set.count(channel->get_name()));
    }

    if (args.is_set("set"))
    {
        /* Exit having applied configuration settings. */
        device->close();
        return 0;
    }

    /* Create session and add device. */
    auto session = context->create_session();
    session->add_device(device);

    /* Create output. */
    auto output_format = context->get_output_formats()[args["output_format"]];
    auto output = output_format->create_output(device);

    /* Add datafeed callback. */
    session->add_callback([=] (
        shared_ptr<Device> device,
        shared_ptr<Packet> packet)
    {
        string text = output->receive(packet);
        if (text.length() > 0)
            printf("%s", text.c_str());
    });

    if (args.is_set("input_file"))
    {
        /* Load data from file. */
        ifdevice->load();
        session->stop();
    }
    else
    {
        /* Start capture. */
        session->start();
        if (args.is_set("continuous"))
        {
            /* Continuous capture, set SIGINT handler to allow stopping. */
            sigint_handler = [=] () { session->stop(); };
            signal(SIGINT, sigint);
        }

        /* Run event loop. */
        session->run();

        /* Clean up. */
        if (args.is_set("continuous"))
            sigint_handler = nullptr;
        else
            session->stop();

        /* Close device. */
        device->close();
    }

    return 0;
}
