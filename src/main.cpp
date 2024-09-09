/*
 Copyright © 2023 Insoft. All rights reserved.
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstring>
#include <iomanip>

#include <sys/time.h>

#include "Singleton.hpp"
#include "common.hpp"

#include "Preprocessor.hpp"
#include "Strings.hpp"

#include "build.h"

using namespace ppl;

static Preprocessor preprocessor = Preprocessor();
static Strings strings = Strings();



void terminator() {
  std::cout << MessageType::Error << "An internal preprocessing problem occurred. Please review the syntax before this point.\n";
  exit(-1);
}
 
void (*old_terminate)() = std::set_terminate(terminator);


void preProcess(std::string &ln, std::ofstream &outfile);

// MARK: - Utills


void reduce(std::string &str) {
    std::regex r;
    std::smatch m;
    
    str = std::regex_replace(str, std::regex(R"(==)"), "=");
    
    if (regex_search(str, std::regex(R"(LOCAL .*)")))
        return;
    
    while (regex_search(str, m, std::regex(R"([A-Za-z]\w*:=[^;]*)"))) {
        std::string matched = m.str();
        
        /*
         eg. v1:=v2+v4;
         Group  0 v1:=v2+v4;
                1 v1
                2 v2+v4
        */
        r = R"(([A-Za-z]\w*):=(.*))";
        auto it = std::sregex_token_iterator {
            matched.begin(), matched.end(), r, {2, 1}
        };
        if (it != std::sregex_token_iterator()) {
            std::stringstream ss;
            ss << *it++ << "▶" << *it;
            str = str.replace(m.position(), m.length(), ss.str());
        }
    }
    
    if (Singleton::Scope::Local == Singleton::shared()->scope) {
        while (regex_search(str, m, std::regex(R"(\b[A-Za-z]\w* *\( *\))"))) {
            std::string matched = m.str();
            matched = regex_replace(matched, std::regex(R"( *\( *\))"), "");
            str = str.replace(m.position(), m.length(), matched);
        }
    }
}

uint32_t utf8_to_utf16(const char *utf8) {
    uint8_t *utf8_char = (uint8_t *)utf8;
    uint16_t utf16_char = *utf8_char;
    
    if ((utf8_char[0] & 0b11110000) == 0b11100000) {
        utf16_char = utf8_char[0] & 0b11111;
        utf16_char <<= 6;
        utf16_char |= utf8_char[1] & 0b111111;
        utf16_char <<= 6;
        utf16_char |= utf8_char[2] & 0b111111;
        return utf16_char;
    }
    
    // 110xxxxx 10xxxxxx
    if ((utf8_char[0] & 0b11100000) == 0b11000000) {
        utf16_char = utf8_char[0] & 0b11111;
        utf16_char <<= 6;
        utf16_char |= utf8_char[1] & 0b111111;
        return utf16_char;
    }
    
    return utf16_char;
}

std::string utf16_to_utf8(const uint16_t* utf16_str, size_t utf16_size) {
    std::string utf8_str;
    
    for (size_t i = 0; i < utf16_size; ++i) {
        uint16_t utf16_char = utf16_str[i];
        
#ifndef __LITTLE_ENDIAN__
        utf16_char = utf16_char >> 8 | utf16_char << 8;
#endif

        if (utf16_char < 0x0080) {
            // 1-byte UTF-8
            utf8_str += static_cast<char>(utf16_char);
        }
        else if (utf16_char < 0x0800) {
            // 2-byte UTF-8
            utf8_str += static_cast<char>(0xC0 | ((utf16_char >> 6) & 0x1F));
            utf8_str += static_cast<char>(0x80 | (utf16_char & 0x3F));
        }
        else {
            // 3-byte UTF-8
            utf8_str += static_cast<char>(0xE0 | ((utf16_char >> 12) & 0x0F));
            utf8_str += static_cast<char>(0x80 | ((utf16_char >> 6) & 0x3F));
            utf8_str += static_cast<char>(0x80 | (utf16_char & 0x3F));
        }
    }
    
    return utf8_str;
}

// TODO: .hpprgrm file format detection and handling.
bool isHPPrgrmFileFormat(std::ifstream &infile)
{
    return false;
}

bool isUTF16le(std::ifstream &infile)
{
    if (!infile.is_open()) return false;
    
    uint16_t word;
    infile.read((char *)&word, 2);
    
#ifdef __LITTLE_ENDIAN__
    word = word >> 8 | word << 8;
#endif
    if (word == 0xFFFE) return true;
    
    infile.seekg(0);
    return false;
}

// MARK: - Pre-Processing...

void processLine(const std::string& str, std::ofstream &outfile)
{
    Singleton& singleton = *Singleton::shared();
    std::string ln = str;
    
    preProcess(ln, outfile);
    
    for ( int n = 0; n < ln.length(); n++) {
        uint8_t *ascii = (uint8_t *)&ln.at(n);
        if (ln.at(n) == '\r') continue;
        
        // Output as UTF-16LE
        if (*ascii >= 0x80) {
            uint16_t utf16 = utf8_to_utf16(&ln.at(n));
            
#ifndef __LITTLE_ENDIAN__
            utf16 = utf16 >> 8 | utf16 << 8;
#endif
            outfile.write((const char *)&utf16, 2);
            if ((*ascii & 0b11100000) == 0b11000000) n++;
            if ((*ascii & 0b11110000) == 0b11100000) n+=2;
            if ((*ascii & 0b11111000) == 0b11110000) n+=3;
        } else {
            outfile.put(ln.at(n));
            outfile.put('\0');
        }
    }
    
    singleton.incrementLineNumber();
}

void processLines(std::ifstream &infile, std::ofstream &outfile)
{
    std::string s;
    
    char c;
    while (!infile.eof()) {
        infile.get(c);
        s += c;
        if (c == 0x0A) {
            infile.seekg(1, std::ios_base::cur);
            processLine(s, outfile);
            s = std::string("");
        }
        
        infile.peek();
    }
}

void processStringLines(std::istringstream &iss, std::ofstream &outfile)
{
    std::string s;
    
    while(getline(iss, s)) {
        processLine(s, outfile);
    }
}

void process(std::ifstream &infile, std::ofstream &outfile)
{
    if (!isUTF16le(infile)) {
        infile.close();
        return;
    }
   
    processLines(infile, outfile);
}


void processString(const std::string &str, std::ofstream &outfile) {
    Singleton &singleton = *Singleton::shared();
    
    std::string ln;
    
    singleton.pushPathname("");

    std::istringstream iss{ str };
    
    processStringLines(iss, outfile);

    singleton.popPathname();
}


void preProcess(std::string &ln, std::ofstream &outfile) {
    std::regex r;
    std::smatch m;
    std::ifstream infile;
    
    Singleton *singleton = Singleton::shared();
    
    static int variableAliasCount = -1, functionAliasCount = -1;
    
    // The UTF16-LE first needs to be converted to UTF8 before it can be proccessed.
    uint16_t *utf16_str = (uint16_t *)ln.c_str();
    ln = utf16_to_utf8(utf16_str, ln.size() / 2);
    
    // To simplifie reg-ex we need to deal with any tabs used in the UTF8 data.
    ln = regex_replace(ln, std::regex(R"(\t)"), " "); // Convert all Tabs to spaces :- future reg-ex will not require to deal with '\t', only spaces.
    
    if (preprocessor.python) {
        // We're presently handling Python code.
        preprocessor.parse(ln);
        ln += '\n';
        return;
    }
    
    if (preprocessor.parse(ln)) {
        if (preprocessor.python) {
            // Indicating Python code ahead with the #PYTHON preprocessor, we maintain the line unchanged and return to the calling function.
            ln += '\n';
            return;
        }
        
        ln = std::string("");
        return;
    }
    
    /*
     While parsing the contents, strings may inadvertently undergo parsing, leading to potential disruptions in the string's content.
     To address this issue, we prioritize the preservation of any existing strings. Subsequently, after parsing, any strings that have
     been universally altered can be restored to their original state.
     */
    strings.preserveStrings(ln);
    
    // Remove any comments.
    size_t pos = ln.find("//");
    if (pos != std::string::npos) {
        ln.resize(pos);
    }

    // Remove any leading white spaces before or after.
    trim(ln);
    
    ln = std::regex_replace(ln, std::regex(R"(  +)"), " ");
    
    ln = regex_replace(ln, std::regex(R"(>=)"), "≥");
    ln = regex_replace(ln, std::regex(R"(<=)"), "≤");
    ln = regex_replace(ln, std::regex(R"(!=)"), "≠");
    
    ln = std::regex_replace(ln, std::regex(R"( *\[ *)"), "[");
    ln = std::regex_replace(ln, std::regex(R"( *\] *)"), "]");
    ln = std::regex_replace(ln, std::regex(R"( *\{ *)"), "{");
    ln = std::regex_replace(ln, std::regex(R"( *\} *)"), "}");
    ln = std::regex_replace(ln, std::regex(R"( *\( *)"), "(");
    ln = std::regex_replace(ln, std::regex(R"( *\) *)"), ")");
    
    ln = regex_replace(ln, std::regex(R"( *> *)"), ">");
    ln = regex_replace(ln, std::regex(R"( *< *)"), "<");
    ln = regex_replace(ln, std::regex(R"( *≥ *)"), "≥");
    ln = regex_replace(ln, std::regex(R"( *≤ *)"), "≤");
    ln = regex_replace(ln, std::regex(R"( *≠ *)"), "≠");

    ln = regex_replace(ln, std::regex(R"( *▶ *)"), "▶");
    ln = regex_replace(ln, std::regex(R"( *\, *)"), ",");
    
    ln = regex_replace(ln, std::regex(R"( *\+ *)"), "+");
    ln = regex_replace(ln, std::regex(R"( *- *)"), "-");
    ln = regex_replace(ln, std::regex(R"( *\/ *)"), "/");
    ln = regex_replace(ln, std::regex(R"( *\* *)"), "*");
    
    ln = regex_replace(ln, std::regex(R"( *:= *)"), ":=");
    ln = regex_replace(ln, std::regex(R"( *= *)"), "=");
    ln = regex_replace(ln, std::regex(R"( *== *)"), "==");

    
//    reduce(ln);
    
    if (regex_match(ln, std::regex(R"(\bBEGIN\b)", std::regex_constants::icase))) {
        singleton->aliases.removeAllLocalAliases();
        variableAliasCount = -1;
    }
    
    r = std::regex(R"(\b(?:BEGIN|IF|CASE|FOR|WHILE|REPEAT|FOR|WHILE|REPEAT)\b)", std::regex_constants::icase);
    for(auto it = std::sregex_iterator(ln.begin(), ln.end(), r); it != std::sregex_iterator(); ++it) {
        singleton->nestingLevel++;
        singleton->scope = Singleton::Scope::Local;
    }
    
    r = std::regex(R"(\b(?:END|UNTIL)\b)", std::regex_constants::icase);
    for(auto it = std::sregex_iterator(ln.begin(), ln.end(), r); it != std::sregex_iterator(); ++it) {
        singleton->nestingLevel--;
        if (0 == singleton->nestingLevel) {
            singleton->scope = Singleton::Scope::Global;
        }
    }
    
    if (Singleton::Scope::Global == singleton->scope) {
        // LOCAL
        ln = regex_replace(ln, std::regex(R"(\bLOCAL +)"), "");
        
        // Function
        r = R"(^[A-Za-z]\w*\((?:[A-Za-z]\w*,?)*\))";
        if (regex_search(ln, m, r)) {
            Aliases::TIdentity identity;
            identity.scope = Aliases::Scope::Global;
            identity.type = Aliases::Type::Function;
            identity.identifier = m.str().substr(0, m.str().find("("));
            
            std::ostringstream os;
            os << "f" << ++functionAliasCount;
            identity.real = os.str();
            
            singleton->aliases.append(identity);
        }
    }
    
    if (Singleton::Scope::Local == singleton->scope) {
        // LOCAL
        r = R"(\bLOCAL (?:[A-Za-z]\w*[,;])+)";
        if (regex_search(ln, m, r)) {
            std::string matched = m.str();
            r = R"([A-Za-z]\w*(?=[,;]))";
            
            for(auto it = std::sregex_iterator(matched.begin(), matched.end(), r); it != std::sregex_iterator(); ++it) {
                if (it->str().length() < 3) continue;
                Aliases::TIdentity identity;
                identity.scope = Aliases::Scope::Local;
                identity.type = Aliases::Type::Variable;
                identity.identifier = it->str();
                
                std::ostringstream os;
                os << "v" << ++variableAliasCount;
                identity.real = os.str();
                
                singleton->aliases.append(identity);
            }
        }
    }
    
    ln = singleton->aliases.resolveAliasesInText(ln);
    
    reduce(ln);
    
    strings.restoreStrings(ln);
    
    ln += '\n';
    ln = regex_replace(ln, std::regex(R"(; +)"), ";");
    ln = regex_replace(ln, std::regex(R"(;\n$)"), ";");
}

// MARK: - Command Line
void version(void) {
    std::cout 
    << "P+ Pre-Processor v"
    << (unsigned)__BUILD_NUMBER / 100000 << "."
    << (unsigned)__BUILD_NUMBER / 10000 % 10 << "."
    << (unsigned)__BUILD_NUMBER / 1000 % 10 << "."
    << std::setfill('0') << std::setw(3) << (unsigned)__BUILD_NUMBER % 1000
    << "\n";
}

void error(void) {
    printf("pplmin: try 'pplmin --help' for more information\n");
}

void info(void) {
    std::cout << "Copyright (c) 2024 Insoft. All rights reserved\n";
    int rev = (unsigned)__BUILD_NUMBER / 1000 % 10;
    std::cout << "PPL Minifier v" << (unsigned)__BUILD_NUMBER / 100000 << "." << (unsigned)__BUILD_NUMBER / 10000 % 10 << (rev ? "." + std::to_string(rev) : "") << " BUILD " << std::setfill('0') << std::setw(3) << __BUILD_NUMBER % 1000 << "\n\n";
}

void usage(void) {
    info();
    std::cout << "usage: pplmin in-file\n\n";
    std::cout << " -v, --verbose     display detailed processing information\n";
    std::cout << " -h, --help        help.\n";
    std::cout << " --version         displays the full version number.\n";
}


// MARK: - Main
int main(int argc, char **argv) {
    std::string in_filename, out_filename;

    if ( argc == 1 )
    {
        error();
        exit(100);
    }
    
    for( int n = 1; n < argc; n++ ) {
        std::string args(argv[n]);
        
        if ( args == "--help" ) {
            usage();
            exit(102);
        }
        
        
        
        if ( strcmp( argv[n], "--version" ) == 0 ) {
            version();
            return 0;
        }
        
        in_filename = argv[n];
        std::regex r(R"(.\w*$)");
        std::smatch extension;
    }
    
    info();
    
    out_filename = in_filename;
    if (out_filename.rfind(".")) {
        out_filename.replace(out_filename.rfind("."), out_filename.length() - out_filename.rfind("."), "-min.hpprgm");
    }
    
    std::ofstream outfile;
    outfile.open(out_filename, std::ios::out | std::ios::binary);
    if(!outfile.is_open())
    {
        error();
        return 0;
    }
    
    std::ifstream infile;
    infile.open(in_filename, std::ios::in | std::ios::binary);
    if(!infile.is_open())
    {
        outfile.close();
        error();
        return 0;
    }
    
    // The "hpprgm" file format requires UTF-16LE.
    
    
    outfile.put(0xFF);
    outfile.put(0xFE);
    
    // Start measuring time
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    std::string str;

//    Singleton::shared()->pushPathname(in_filename);
    process( infile, outfile );
//    Singleton::shared()->popPathname();
    
    // Stop measuring time and calculate the elapsed time.
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Display elasps time in secononds.
    double delta_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;
    printf("Completed in %.3f seconds.\n", delta_us * 1e-6);
    
    infile.close();
    outfile.close();
    
    if (hasErrors() == true) {
        std::cout << "ERRORS!\n";
        remove(out_filename.c_str());
        return 0;
    }
    
    if (!Singleton::shared()->aliases.descendingOrder && Singleton::shared()->aliases.verbose) {
        Singleton::shared()->aliases.dumpIdentities();
    }
    
    std::ifstream::pos_type insize = file_size(in_filename);
    std::ifstream::pos_type outsize = file_size(out_filename);
    std::cout << "File reduction of " << insize - outsize << " bytes.\n";
    
    std::cout << "UTF-16LE File '" << out_filename << "' Succefuly Created.\n";
    
    
    return 0;
}
