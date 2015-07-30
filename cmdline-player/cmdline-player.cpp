#include <functional>
#include <map>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <nxplay/log.hpp>
#include <nxplay/init_gstreamer.hpp>
#include <nxplay/main_pipeline.hpp>
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
		callbacks.m_buffering_updated_callback = [](nxplay::media const &p_media, guint64 const p_token, bool const p_is_current_media, unsigned int const p_percentage)
		{
			std::cerr << "Buffering: " << p_percentage << "  media uri: " << p_media.get_uri() << " token: " << p_token << "  current: " << p_is_current_media << "\n";
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
		callbacks.m_new_tags_callback = [](nxplay::media const &p_current_media, guint64 const p_token, nxplay::tag_list const &p_tag_list)
		{
			std::cerr << "New tags for current media with URI " << p_current_media.get_uri() << " and token " << p_token << ": " << nxplay::to_string(p_tag_list) << "\n";
		};

		nxplay::main_pipeline pipeline(callbacks);


		// Set up command map
		command_map commands;
		commands["play"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				bool now = (p_tokens.size() > 2) ? (p_tokens[2] != "no") : true;
				pipeline.play_media(pipeline.get_new_token(), nxplay::media(p_tokens[1]), now);
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
		commands["setvolume"] =
		{
			[&](cmdline_player::tokens const &p_tokens)
			{
				double volume = std::stod(p_tokens[1]);
				pipeline.set_volume(volume, GST_STREAM_VOLUME_FORMAT_LINEAR);
				return true;
			},
			1, "<volume>",
			"sets the current volume in the 0.0 .. 1.0 range"
		};
		commands["getvolume"] =
		{
			[&](cmdline_player::tokens const &)
			{
				std::cerr << "Current volume: " << pipeline.get_volume(GST_STREAM_VOLUME_FORMAT_LINEAR) << "\n";
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
				pipeline.set_muted(mute);
				return true;
			},
			1, "<mute yes/no>",
			"mutes playback if argument is \"yes\", unmutes otherwise"
		};
		commands["ismuted"] =
		{
			[&](cmdline_player::tokens const &)
			{
				std::cerr << "Is currently muted: " << (pipeline.is_muted() ? "yes" : "no") << "\n";
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
			pipeline.play_media(pipeline.get_new_token(), nxplay::media(argv[1]), true);
		if (argc > 2)
			pipeline.play_media(pipeline.get_new_token(), nxplay::media(argv[2]), false);

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
