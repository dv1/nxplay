#include <assert.h>
#include "tokenizer.hpp"


namespace cmdline_player
{


tokens tokenize_line(std::string const &p_line)
{
	static std::string const whitespace_chars("\t ");
	static std::string const quote_chars("\"'");
	size_t cur_pos = 0;
	size_t start_pos = 0;
	tokens str_tokens;

	if (p_line.empty())
		return tokens();

	while (cur_pos < p_line.length())
	{
		// In this loop, the next token is extracted by finding the next delimiter.
		// start_pos denotes the start position of the current token, cur_pos the
		// current position of the search. cur_pos >= start_pos always holds.

		assert(cur_pos >= start_pos);

		// Try to find the next delimiter
		size_t delim_pos = p_line.find_first_of(whitespace_chars + quote_chars + '\\', cur_pos);

		if (delim_pos == std::string::npos)
		{
			// There is no next token; end of line was reached. Output everything from
			// start_pos to the end as a final token (unless the token is empty).
			if (start_pos < p_line.length())
				str_tokens.push_back(p_line.substr(start_pos));
			break;
		}
		else if (p_line[delim_pos] == '\\')
		{
			// A backslash was found. This is not really a limiter, but needs to be treated
			// specially, since if a delimiter character immediately follows, then that
			// character is *not* to be interpreted as a delimiter. Example: abc\"def

			// If there is a character after the backslash, investigate
			if ((delim_pos + 1) < p_line.length())
			{
				// TODO: this distinction is actually unnecessary; just always skip two characters
				if ((whitespace_chars + quote_chars).find(p_line[delim_pos + 1]) != std::string::npos)
				{
					// next character is a delimiter character; skip it and the backlash
					cur_pos = delim_pos + 2;
				}
				else
				{
					// next character is a regular character; just skip the backslash
					cur_pos = delim_pos + 1;
				}
			}

			// If there is no character after the backslash, don't do anything;
			// instead, just output everything from start_pos to the end as one token
			// (unless the token is empty)
			if (((delim_pos + 1) >= p_line.length()) || (cur_pos >= p_line.length()))
			{
				if (start_pos < p_line.length())
					str_tokens.push_back(p_line.substr(start_pos));
				break;
			}
		}
		else if (whitespace_chars.find(p_line[delim_pos]) != std::string::npos)
		{
			// A whitespace charater was found. It is not escaped with a backslash (handled above),
			// and not inside quotes (handled below). Output everything from start_pos to
			// just before the delimiter position as one token (unless the token is empty).

			if (delim_pos > start_pos)
				str_tokens.push_back(p_line.substr(start_pos, delim_pos - start_pos));
			start_pos = cur_pos = delim_pos + 1; // +1 to skip the delimiter
		}
		else if (quote_chars.find(p_line[delim_pos]) != std::string::npos)
		{
			// A quote character was found. These enclose a sequence of characters,
			// and define them as one token, even if whitespace characters are included.
			// Search for a second matching quote character, output everything
			// between start_pos and just before the delimiter position as one token,
			// and the characters between the quotes as another token.
			// Backslashes are treated specially.

			char const quote_char = p_line[delim_pos];

			// First, output everything between start_pos and the first quote
			// unless the token is empty).
			if (delim_pos > start_pos)
				str_tokens.push_back(p_line.substr(start_pos, delim_pos - start_pos));

			// end_token_pos denotes the position of the matching second quote.
			size_t end_token_pos = delim_pos + 1;
			while (end_token_pos < p_line.length())
			{
				// Look for a second quote
				size_t new_pos = p_line.find(quote_char, end_token_pos);

				if (new_pos == std::string::npos)
				{
					// No second quote found; interpret end of line
					// as the end of token
					end_token_pos = std::string::npos;
					break;
				}
				else if (p_line[new_pos - 1] != '\\')
				{
					// Character is a quote, and the character before
					// the qutoe is not a backlash -> second quote found
					end_token_pos = new_pos;
					break;
				}
				else
				{
					// Just before the found quote, a backslash is present,
					// so ignore this quote, and resume search
					end_token_pos = new_pos + 1; // + 1 to skip quote
				}
			}

			// Output quote
			if (end_token_pos == std::string::npos)
			{
				if ((delim_pos + 1) < p_line.length())
					str_tokens.push_back(p_line.substr(delim_pos + 1));
				break;
			}
			else
			{
				if ((end_token_pos - delim_pos - 1) > 0)
					str_tokens.push_back(p_line.substr(delim_pos + 1, end_token_pos - delim_pos - 1));
				start_pos = cur_pos = end_token_pos + 1; // + 1 to move over second quote
			}
		}
	}

	// Second pass; normalize escaped characters by removing the backslashes.
	for (tokens::iterator token_iter = str_tokens.begin(); token_iter != str_tokens.end(); ++token_iter)
	{
		std::string &token = *token_iter;

		for (std::string::iterator char_iter = token.begin(); char_iter != token.end();)
		{
			if ((*char_iter) == '\\')
			{
				if ((char_iter + 1) == token.end())
				{
					// Backlash found, but it is the last character, so ignore
					break;
				}
				else
				{
					// Backslash found, and it is not the last character, so remove it
					// If the next character is also a backlash (thus escaping backslash itself),
					// skip that second backslash
					// So, Hello\"World\\Abc becomes Hello"World\Abc
					char_iter = token.erase(char_iter);
					if ((char_iter != token.end()) && ((*char_iter) == '\\'))
						++char_iter;
				}
			}
			else
				++char_iter;
		}
	}

	return str_tokens;
}


} // namespace cmdline_player end
