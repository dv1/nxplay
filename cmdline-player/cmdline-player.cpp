#include <functional>
#include <map>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <pthread.h>
#include <nxplay/log.hpp>
#include <nxplay/init_gstreamer.hpp>
#include <nxplay/main_pipeline.hpp>
#include <nxplay/soft_volume_control.hpp>
#include "tokenizer.hpp"

extern "C"
{
#include <histedit.h>
}



typedef std::function < bool(cmdline_player::tokens const &p_tokens) > command_function;

struct command_entry
{
	command_function m_function;
	unsigned int m_num_required_arguments;
	std::string m_args_desc;
	std::string m_description;
};

typedef std::map < std::string, command_entry > command_map;


namespace
{


char const * prompt(EditLine *)
{
	return "cmd> ";
}


void print_commands(command_map const &p_command_map)
{
	std::size_t max_cmdnam_length = 0;
	for (auto const &entry : p_command_map)
	{
		std::size_t len = entry.first.length();
		if (entry.second.m_num_required_arguments != 0)
			len += 1 + entry.second.m_args_desc.length();
		max_cmdnam_length = std::max(len, max_cmdnam_length);
	}

	std::cerr << "Command list:\n\n";
	for (auto const &entry : p_command_map)
	{
		std::size_t len = entry.first.length();

		std::cerr << "  " << entry.first;
		if (entry.second.m_num_required_arguments != 0)
		{
			len += 1 + entry.second.m_args_desc.length();
			std::cerr << " " << entry.second.m_args_desc;
		}

		std::cerr << std::setw(max_cmdnam_length - len + 3) << " : " << entry.second.m_description << "\n";
	}
	std::cerr << "\n";
}


}


int main(int argc, char *argv[])
{
	int ret = 0;


#ifdef __APPLE__
    pthread_setname_np("main-thread");
#else
    pthread_setname_np(pthread_self(), "main-thread");
#endif


	// Set up nxplay log
	nxplay::set_min_log_level(nxplay::log_level_trace);
	nxplay::set_stderr_output();


	// Initialize GStreamer
	if (!nxplay::init_gstreamer(&argc, &argv))
	{
		std::cerr << "Could not initialize GStreamer - exiting\n";
		return -1;
	}

	// Initialize libedit
	EditLine *el = el_init(argv[0], stdin, stdout, stderr);
	el_set(el, EL_PROMPT, prompt);
	el_set(el, EL_EDITOR, "emacs");

	// Initialize libedit history
	HistEvent ev;
	History *myhistory = history_init();
	if (myhistory == nullptr)
	{
		std::cerr << "libedit history could not be initialized - exiting\n";
		return -1;
	}
	history(myhistory, &ev, H_SETSIZE, 800);
	el_set(el, EL_HIST, history, myhistory);


	{
		// Create main pipeline and callbacks
		nxplay::main_pipeline::callbacks callbacks;
		callbacks.m_media_started_callback = [](nxplay::media const &p_current_media, guint64 const p_token)
		{
			std::cerr << "Media started with uri " << p_current_media.get_uri() << " and token " << p_token << "\n";
		};
		callbacks.m_end_of_stream_callback = []()
		{
			std::cerr << "End-Of-Stream reported\n";
		};
		callbacks.m_state_changed_callback = [](nxplay::states const p_old_state, nxplay::states const p_new_state)
		{
			std::cerr << "State change: old: " << nxplay::get_state_name(p_old_state) << " new: " << nxplay::get_state_name(p_new_state) << "\n";
		};
		callbacks.m_buffering_updated_callback = [=](nxplay::media const &p_media, guint64 const p_token, bool const p_is_current_media, unsigned int const p_percentage, boost::optional < guint > const p_level, guint const p_limit)
		{
			std::cerr << "Buffering: " << p_percentage << "% (";
			if (p_level)
				std::cerr << *p_level;
			else
				std::cerr << "<undefined>";
			std::cerr << " bytes)  media uri: " << p_media.get_uri() << " token: " << p_token << "  current: " << p_is_current_media << " limit: " << p_limit << "\n";
		};
		callbacks.m_packet_loss_callback = [](nxplay::media const &p_current_media, guint64 const p_token, unsigned int const p_packet_count)
		{
			std::cerr << "Packet loss detected: " << p_packet_count << " packet(s) lost, current media URI: " << p_current_media.get_uri() << " token: " << p_token << "\n";
		};
		callbacks.m_duration_updated_callback = [](nxplay::media const &p_current_media, guint64 const p_token, gint64 const p_new_duration, nxplay::position_units const p_unit)
		{
			switch (p_unit)
			{
				case nxplay::position_unit_nanoseconds:
					std::cerr << "Current duration for media with URI " << p_current_media.get_uri() << " and token " << p_token << " in ms: " << (p_new_duration / GST_MSECOND) << "\n";
					break;
				case nxplay::position_unit_bytes:
					std::cerr << "Current duration for media with URI " << p_current_media.get_uri() << " and token " << p_token << " in bytes: " << p_new_duration << "\n";
					break;
			}
		};
		callbacks.m_position_updated_callback = [](nxplay::media const &p_current_media, guint64 const p_token, gint64 const p_new_position, nxplay::position_units const p_unit)
		{
			switch (p_unit)
			{
				case nxplay::position_unit_nanoseconds:
					std::cerr << "Current position for media with URI " << p_current_media.get_uri() << " and token " << p_token << " in ms: " << (p_new_position / GST_MSECOND) << "\n";
					break;
				case nxplay::position_unit_bytes:
					std::cerr << "Current position for media with URI " << p_current_media.get_uri() << " and token " << p_token << " in bytes: " << p_new_position << "\n";
					break;
			}
		};
		callbacks.m_buffer_level_callback = [](nxplay::media const &p_current_media, guint64 const p_token, guint const p_level, guint const p_limit)
		{
			std::cerr << "Buffer level of media with URI " << p_current_media.get_uri() << " and token " << p_token << ": " << p_level << " bytes " << "  limit: " << p_limit << " bytes\n";
		};
		callbacks.m_media_about_to_end_callback = [](nxplay::media const &p_current_media, guint64 const p_token)
		{
			std::cerr << "Media with uri " << p_current_media.get_uri() << " and token " << p_token << " about to end\n";
		};
		callbacks.m_info_callback = [](std::string const &p_info_message)
		{
			std::cerr << "Info message: " << p_info_message << "\n";
		};
		callbacks.m_warning_callback = [](std::string const &p_warning_message)
		{
			std::cerr << "Warning message: " << p_warning_message << "\n";
		};
		callbacks.m_error_callback = [](std::string const &p_error_message)
		{
			std::cerr << "Error message: " << p_error_message << "\n";
		};
		callbacks.m_is_seekable_callback = [](nxplay::media const &p_media, guint64 const p_token, bool const p_is_current_media, bool const p_is_seekable)
		{
			std::cerr << (p_is_current_media ? "Current" : "Next") << " media with URI " << p_media.get_uri() << " and token " << p_token << " is seekable: " << p_is_seekable << "\n";
		};
		callbacks.m_is_live_callback = [](nxplay::media const &p_media, guint64 const p_token, bool const p_is_current_media, bool const p_is_live)
		{
			std::cerr << (p_is_current_media ? "Current" : "Next") << " media with URI " << p_media.get_uri() << " and token " << p_token << " is live: " << p_is_live << "\n";
		};
		callbacks.m_new_tags_callback = [](nxplay::media const &p_current_media, guint64 const p_token, nxplay::tag_list const &p_tag_list)
		{
			std::cerr << "New tags for current media with URI " << p_current_media.get_uri() << " and token " << p_token << ": " << nxplay::to_string(p_tag_list) << "\n";
		};

		nxplay::thread_sched_settings sched_settings =
		{
			SCHED_RR, sched_get_priority_min(SCHED_RR) + 1,
			SCHED_RR, sched_get_priority_min(SCHED_RR) + 0,
			SCHED_RR, sched_get_priority_min(SCHED_RR) + 0,
			SCHED_OTHER, sched_get_priority_min(SCHED_OTHER)
		};

		nxplay::soft_volume_control volobj;
		nxplay::main_pipeline pipeline(callbacks, GST_SECOND * 5, 500, false, { &volobj }, sched_settings);


		nxplay::playback_properties props;
		props.m_allowed_transports = nxplay::transport_protocol_tcp;
		props.m_jitter_buffer_length = 1500;
		props.m_do_retransmissions = true;


		// Set up command map
		command_map commands;
		commands["play"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				bool now = (p_tokens.size() > 2) ? (p_tokens[2] != "no") : true;
				pipeline.play_media(pipeline.get_new_token(), nxplay::media(p_tokens[1]), now, props);
				return true;
			},
			1, "<URI> <now yes/no>",
			"plays new media with a given URI; if the second parameter is \"no\", the media will be played after the current one, or right now if nothing is currently playing"
		};
		commands["pause"] =
		{
			[&](cmdline_player::tokens const &p_tokens) { pipeline.set_paused(p_tokens[1] == "yes"); return true; },
			1, "<pause yes/no>",
			"pauses any current playback; if the parameter is \"yes\", pauses, otherwise unpauses; if nothing is playing, this call is ignored"
		};
		commands["ispaused"] =
		{
			[&](cmdline_player::tokens const &)
			{
				std::cerr << "Is currently paused: " << ((pipeline.get_current_state() == nxplay::state_paused) ? "yes" : "no") << "\n";
				return true;
			},
			0, "",
			"checks if playback is currently paused"
		};
		commands["stop"] =
		{
			[&](cmdline_player::tokens const &) { pipeline.stop(); return true; },
			0, "",
			"stops any current playback"
		};
		commands["seek"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				gint64 pos = std::stoll(p_tokens[1]);
				pipeline.set_current_position(pos * GST_MSECOND, nxplay::position_unit_nanoseconds);
				return true;
			},
			1, "<seek position in milliseconds>",
			"seeks to the given position if playback allows for seeking"
		};
		commands["tell"] =
		{
			[&](cmdline_player::tokens const &) { std::cerr << "Current position in ms: " << pipeline.get_current_position(nxplay::position_unit_nanoseconds) / GST_MSECOND; return true; },
			0, "",
			"prints the curent playback position in milliseconds"
		};
		commands["setbufsizelimit"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				guint size = std::stol(p_tokens[1]);
				pipeline.set_buffer_size_limit(size);
				return true;
			},
			1, "<buffer size>",
			"sets the size limit of the current stream's buffer, in bytes"
		};
		commands["setbufestdur"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				guint64 duration = std::stoll(p_tokens[1]) * GST_MSECOND;
				pipeline.set_buffer_estimation_duration(duration);
				return true;
			},
			1, "<buffer size>",
			"sets the duration for current stream's bitrate-based buffer size estimations, in milliseconds"
		};
		commands["setbuftimeout"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				guint64 timeout = std::stoll(p_tokens[1]) * GST_MSECOND;
				pipeline.set_buffer_timeout(timeout);
				return true;
			},
			1, "<buffer size>",
			"sets the current stream's buffer timeout, in milliseconds"
		};
		commands["setbufthresholds"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				guint low = std::stoi(p_tokens[1]);
				guint high = std::stoi(p_tokens[2]);
				pipeline.set_buffer_thresholds(low, high);
				return true;
			},
			2, "<low threshold> <high threshold>",
			"sets the current stream's buffer timeout, in milliseconds"
		};
		commands["setvolume"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				double volume = std::stod(p_tokens[1]);
				volobj.set_volume(volume);
				return true;
			},
			1, "<volume>",
			"sets the current volume in the 0.0 .. 1.0 range"
		};
		commands["getvolume"] =
		{
			[&](cmdline_player::tokens const &)
			{
				std::cerr << "Current volume: " << volobj.get_volume() << "\n";
				return true;
			},
			0, "",
			"gets the current volume in the 0.0 .. 1.0 range"
		};
		commands["mute"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				bool mute = (p_tokens[1] == "yes");
				volobj.set_muted(mute);
				return true;
			},
			1, "<mute yes/no>",
			"mutes playback if argument is \"yes\", unmutes otherwise"
		};
		commands["ismuted"] =
		{
			[&](cmdline_player::tokens const &)
			{
				std::cerr << "Is currently muted: " << (volobj.is_muted() ? "yes" : "no") << "\n";
				return true;
			},
			0, "",
			"checks if playback is currently muted"
		};
		commands["help"] =
		{
			[&](cmdline_player::tokens const &) { print_commands(commands); return true; },
			0, "",
			"lists the commands"
		};
		commands["quit"] =
		{
			[](cmdline_player::tokens const &) { return false; },
			0, "",
			"exits the player"
		};

		std::cerr << "Type help to get a list of valid commands\n\n";

		if (argc > 1)
			pipeline.play_media(pipeline.get_new_token(), nxplay::media(argv[1]), true, props);
		if (argc > 2)
			pipeline.play_media(pipeline.get_new_token(), nxplay::media(argv[2]), false, props);

		try
		{
			bool loop = true;

			while (loop)
			{
				int count;
				char const *line_cstr = el_gets(el, &count);

				if ((count <= 0) || (line_cstr == nullptr))
					continue;

				std::string line(line_cstr);
				line.pop_back(); // get rid of the newline
				if (line.empty())
					continue;

				history(myhistory, &ev, H_ENTER, line.c_str());

				cmdline_player::tokens tokens = cmdline_player::tokenize_line(line);
				if (tokens.empty())
					continue;

				auto const &command = tokens[0];
				auto const &entry = commands.find(command);

				if (entry == commands.end())
				{
					std::cerr << "Unknown command \"" << command << "\"\n";
					continue;
				}

				if (tokens.size() < (entry->second.m_num_required_arguments + 1))
				{
					std::cerr << "Not enough arguments: expected: " << entry->second.m_num_required_arguments << " got: " << (tokens.size() - 1) << "\n";
					std::cerr << "  Usage: " << entry->first << " " << entry->second.m_args_desc << "\n";
					continue;
				}

				if (!(entry->second.m_function(tokens)))
					break;
			}
		}
		catch (std::exception const &p_ex)
		{
			std::cerr << "Exception raised: " << p_ex.what() << "\n";
			ret = -1;
		}


	}


	// Cleanup libedit & its history
	history_end(myhistory);
	el_end(el);

	// Deinitialize GStreamer
	nxplay::deinit_gstreamer();


	return ret;
}
