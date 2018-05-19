#include <iostream>
#include <vector>
#include <queue>
#include <map>
#include <fstream>
#include <string.h>
#include <cassert>
#include <string>
#include <stdexcept>

using namespace std::string_literals;

/**
 * rbx - data ptr
 * r12 - putchar
 * r13 - getchar
 */

const auto prologue_code =
  "\x55"              // push %rbp
  "\x48\x89\xe5"      // mov %rsp, %rbp
  "\x4c\x8b\x27"      // mov (%rdi), %r12
  "\x4c\x8b\x6f\x08"  // mov 8(%rdi), %r13
  "\x48\x89\xd3"s;    // mov %rdx, %rbx

const auto epilogue_code = "\xc9\xc3"s; // leave; ret

const auto right_code = "\x48\x83\xc3\x01"s;  // add $1, %rbx
const auto left_code  = "\x48\x83\xeb\x01"s;  // sub $1, %rbx

const auto plus_code  = "\x80\x03\x01"s;  // addb $1, (%rbx)
const auto minus_code = "\x80\x2b\x01"s;  // subb $1, (%rbx)

const auto out_code = 
  "\x48\x0f\xb6\x3b"  // mov (%rbx), %rdi
  "\x41\xff\xd4"s;    // call *%r12

const auto in_code = 
  "\x41\xff\xd5"      // call *%r13
  "\x88\x03"s;        // mov %rax, (%rbx)

const auto call_loop_code =
  "\x80\x3b\x00"      // cmp $0, %(rbx)
  "\x74\x07"          // je . + 10
  "\xe8"              // call *n(%rip)
  "\xde\xad\xbe\xef"  // n
  "\xeb\xf4"s;        // jmp . - 11

const auto loop_end_code = "\xc3"s; // ret

enum class token_t {
  program, loop, whitespace,
  plus = '+', minus = '-',
  right = '>', left = '<',
  in = ',', out = '.'
};

auto assembler = std::map<token_t, std::string> {
  {token_t::plus, plus_code}, {token_t::minus, minus_code},
  {token_t::left, left_code}, {token_t::right, right_code},
  {token_t::in, in_code},     {token_t::out, out_code},
  {token_t::loop, call_loop_code},
  {token_t::whitespace, ""}
};

auto char_to_token = std::map<char, token_t> {
  {'+', token_t::plus},  {'-', token_t::minus},
  {'>', token_t::right}, {'<', token_t::left},
  {'.', token_t::out},   {',', token_t::in},
};

auto find_closing_bracket(auto begin, auto end) {
  assert(*begin == '[');
  int level = 0;

  for (auto it = begin; it != end; ++it) {
    if (*it == '[')
      ++level;
    else if (*it == ']' && --level == 0)
      return it;
  }
  throw std::runtime_error{"unmatch bracket."};
}

auto next_token(auto it, auto end) {
  auto c = *it;

  if (c == '+' || c == '-' || c == '>' || c == '<') {
    do ++it;
    while (it != end && *it == c);
    return std::make_pair(char_to_token.at(c), it);

  } else if (c == '[') {
    auto close = find_closing_bracket(it, end);
    return std::make_pair(token_t::loop, close);

  } else if (c == '.' || c == ',') {
    return std::make_pair(char_to_token.at(c), ++it);
  }

  return std::make_pair(token_t::whitespace, ++it);
}

auto gather_tokens(auto it, auto end) {
  auto tokens = std::vector<std::pair<token_t, const char*>>{};

  while (it != end) {
    auto [token, next] = next_token(it, end);
    tokens.emplace_back(std::make_pair(token, next));
    assert(next <= end);
    it = next;
  }
  return tokens;
}

auto compile_loops(auto& output, auto it, auto const& tokens) {
  auto offsets = std::queue<long>{};

  for (auto [token, next]: tokens) {
    if (token == token_t::loop) {
      auto offset = compile(output, it+1, next, token);
      offsets.emplace(offset);
    }
    it = next;
  }
  return offsets;
}

void emit_machine_code(auto& output, auto it, auto token, auto& tokens, auto& offsets) {
  if (token == token_t::program)
    output += prologue_code;

  for (auto [token, next] : tokens) {
    auto here = output.length();
    output += assembler[token];

    auto size = std::distance(it, next);
    if (token == token_t::loop) {
        assert(!offsets.empty());
        auto jump = offsets.front() - here - 10;
        offsets.pop();
        output += call_loop_code;
        memcpy(&output[here+6], (char *)&jump, 4);

    } else if (token == token_t::plus || token == token_t::minus || token == token_t::left || token == token_t::right) {
      memcpy(&output[output.length()-1], (char *)&size, 1);
    }
    it = next;
  }
  assert(offsets.empty());

  if (token == token_t::loop)
    output += loop_end_code;
  else if (token == token_t::program)
    output += epilogue_code;
}

auto compile(auto& output, const char* begin, const char* end, token_t token=token_t::program) -> long {
  auto tokens = gather_tokens(begin, end);
  auto offsets = compile_loops(output, begin, tokens);
  auto offset = output.length();

  emit_machine_code(output, begin, token, tokens, offsets);
  return offset;
}

auto bf_getchar() -> decltype(getchar()) {
  auto c = getchar();
  return c == EOF ? 0 : c;
}

auto compile(auto const& source) {
  auto program = ""s;
  auto main_offset = compile(program, &source[0], &source.back());

  return [=]() {
    char data[1024*1024] = {0};
    void *lib[] = {
      (void *)&putchar,
      (void *)&bf_getchar
    };

    auto bf_main = (void (*)(void *, void *, void *))(program.c_str() + main_offset);
    bf_main(lib, nullptr, &data);
  };
}

int main(int argc, char *argv[]) try {
  if (argc != 2) throw std::runtime_error{"usage: "s + argv[0] + " <file>"};

  auto file = std::ifstream{argv[1]};
  if (!file) throw std::runtime_error{"error opening file"};

  auto source = std::string {
    std::istreambuf_iterator<char>(file),
    std::istreambuf_iterator<char>()
  };

  auto program = compile(source);
  program();

} catch (std::runtime_error err) {
  std::cerr << err.what() << std::endl;
  return 1;
}
