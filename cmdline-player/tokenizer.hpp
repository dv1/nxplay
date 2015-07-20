#ifndef CMDLINE_PLAYER_TOKENIZER_HPP
#define CMDLINE_PLAYER_TOKENIZER_HPP

#include <vector>
#include <string>


namespace cmdline_player
{


typedef std::vector < std::string > tokens;

tokens tokenize_line(std::string const &p_line);


} // namespace cmdline_player end


#endif
