/*
 * Copyright 2015-2016 Nicholas Andrews
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#ifndef __NN_READER_HPP__
#define __NN_READER_HPP__

#include <iomanip>
#include <system_error>
#include <cctype>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>

#include <nn/log.hpp>
#include <nn/utf8.hpp>
#include <nn/mutable_symtab.hpp>
#include <nn/data.hpp>

#include <cereal/types/unordered_map.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>

namespace nn {

    bool slow_is_number(std::string s) {
        std::replace( s.begin(), s.end(), ',', '.');
        bool is_a_number = false;
        try {
            boost::lexical_cast<double>(s);
            is_a_number = true;
        }
        catch(boost::bad_lexical_cast &) {
            // if it throws, it's not a number.
        }
        return is_a_number;
    }

    bool is_number(const std::string &s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
    }

    std::vector<std::string> &split(const std::string &s,
                                    char delim,
                                    std::vector<std::string> &elems) {
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, delim)) {
            elems.push_back(item);
        }
        return elems;
    }

    std::vector<std::string> split(const std::string &s, char delim) {
        std::vector<std::string> elems;
        split(s, delim, elems);
        return elems;
    }

    // https://github.com/nemtrif/utfcpp
    std::vector<std::string> split_utf8_word(std::string word) {
        std::vector<std::string> ret;
        const char *str = word.c_str();
        const char* str_i = str;
        const char* end = str+strlen(str)+1;
        unsigned char u[5] = {0,0,0,0,0};
        do {
            // get 32 bit code of a utf-8 symbol
            uint32_t code = utf8::next(str_i, end);
            if (code == 0) continue;
            unsigned char* end_of_char = utf8::append(code, u);
            std::string char_str(reinterpret_cast<char*>(u));
            ret.push_back(char_str);
            std::fill(u, u+5, 0);
        } while ( str_i < end );
        return ret;
    }

    std::vector<std::string> split_tag(std::string tag, std::string other_tag) {
        size_t found = tag.find("-");
        if(found != std::string::npos) {
            return split(tag, '-');
        }
        std::vector<std::string> tokens;
        tokens.push_back("B");
        CHECK(tag == other_tag) << "unexpected tag: " << tag;
        tokens.push_back(tag);
        return tokens;
    }

    template<typename k_type = size_t,      // key type
             typename s_type = std::string, // symbol value type
             typename t_type = std::string  // tag value type
             >
    class CoNLLCorpus {
    public:
        mutable_symbol_table<k_type,s_type> symtab;
        mutable_symbol_table<k_type,t_type> tagtab;

        // This maps from encoded observations to their representation
        // in the context representation in a sentence. This map is
        // created once while the data is loaded and then never
        // modified.
        // mutable_symbol_table<k_type, std::vector<k_type> > context_map;
        std::unordered_map<std::vector<size_t>, size_t> context_map;
        std::unordered_map<size_t, std::vector<size_t> > context_tag_keys;

        // Unique words appearing in the data; this is used to
        // represent the context during inference. We normalize things
        // like numbers to distinguished values, and uppercase
        // everything.
        mutable_symbol_table<k_type, s_type> vocab;

        k_type bos;
        k_type eos;
        k_type space;
        k_type unk;
        k_type other_tag;

        bool frozen { false };

        std::string unk_tag {"?"}; // string value for unknown (latent) tags

    public:
        template<class Archive>
        void serialize(Archive & archive) {
            archive( symtab,
                     tagtab,
                     context_map,
                     context_tag_keys,
                     vocab,
                     bos,
                     eos,
                     space,
                     unk,
                     other_tag,
                     frozen,
                     unk_tag );
        }

        CoNLLCorpus() {}
        CoNLLCorpus(s_type _bos,   // beginning of sequence
                    s_type _eos,   // end of sequence
                    s_type _space, // space input symbol
                    s_type _unk,   // distinguished unknown sym (sentinel value)
                    t_type _other  // other tag
            ) {
            bos       = symtab.add_key(_bos);
            eos       = symtab.add_key(_eos);
            space     = symtab.add_key(_space);
            unk       = symtab.add_key(_unk);
            other_tag = tagtab.add_key(_other);
        }

        const s_type& get_bos_val()   const { return symtab.val(bos);       }
        const s_type& get_eos_val()   const { return symtab.val(eos);       }
        const s_type& get_space_val() const { return symtab.val(space);     }
        const s_type& get_unk_val()   const { return symtab.val(unk);       }
        const t_type& get_other_val() const { return tagtab.val(other_tag); }

        k_type get_bos_key()          const { return bos;                   }
        k_type get_eos_key()          const { return eos;                   }
        k_type get_space_key()        const { return space;                 }
        k_type get_unk_key()          const { return unk;                   }
        k_type get_other_key()        const { return other_tag;             }

        // TODO: avoid creating a new vector
        syms get_eos_obs() const {
            syms EOS { 0, eos, 0 };
            return EOS;
        }

        // TODO: avoid creating a new vector
        syms get_bos_obs() const {
            syms BOS { 0, bos, 0 };
            return BOS;
        }

        size_t get_bos_context_key() const {
            get_word_context_code( get_bos_obs() );
        }

        // This is just used while initializing the
        std::string get_tag_context_string(size_t tag) const {
            if (tag == other_tag) {
                throw std::logic_error("called on context tag");
            }
            auto tagstr = tagtab.val(tag);
            return "<" + tagstr + ">";
        }

        size_t get_word_context_code(const std::vector<size_t>& encoded_word)
            const {
            //CHECK(context_map.count(encoded_word)) << "missing context key for: "
            //                                       << decode(encoded_word);
            return context_map.at(encoded_word);
        }

        // This is only called once when the corpus is finalized.
        std::vector<size_t> make_tag_context_key(size_t tag) const {
            std::vector<size_t> ret {0, tag, 0};
            return ret;
        }

        // This will be called repeatedly during inference.
        const std::vector<size_t>& get_tag_context_vector(size_t tag) const {
            return context_tag_keys.at(tag);
        }

        size_t get_tag_context_code(size_t tag) const {
            return context_map.at( get_tag_context_vector(tag) );
        }

        static size_t num_instances(std::string path) {
            std::ifstream infile;
            infile.open(path);
            if(!infile) {
                LOG(FATAL) << "Error reading: `" << path << "'";
                throw std::system_error(EIO, std::generic_category());
            }
            size_t result = 0;
            std::string line;
            while (std::getline(infile, line)) {
                std::vector<std::string> toks;
                boost::split(toks, line, boost::is_any_of(" \n\t"));
                if(toks.size() != 2) { // end of sentence
                    result ++;
                }
            }
            infile.close();
            return result;
        }

        std::string get_tagging_string(std::vector<size_t> tags,
                                       std::vector<size_t> lens) const {
            std::vector<std::string> tagging;
            for(auto i=0; i<tags.size(); ++i) {
                auto tag = tags.at(i);
                auto len = lens.at(i);
                for(auto j=0; j<len; ++j) {
                    if(j==0) {
                        if(tag == other_tag) {
                            tagging.push_back(tagtab.val(tag));
                        } else {
                            tagging.push_back("B-"+tagtab.val(tag));
                        }
                    } else {
                        tagging.push_back("I-"+tagtab.val(tag));
                    }
                }
            }
            return boost::algorithm::join(tagging, " ");
        }

        instance line_to_instance(std::string line) const {
            CHECK(frozen) << "this should be used after training a model";
            instance sentence;
            std::vector<std::string> tokens;
            boost::split(tokens, line, boost::is_any_of(" \t"));
            sentence.chars.push_back(bos);
            for (auto token : tokens) {
                auto chars = split_utf8_word(token);
                auto i = 0;
                std::vector<size_t> word {bos};
                for (auto c : chars) {
                    k_type s;
                    if(symtab.has_key(c)) {
                        s = symtab.key(c);
                    } else {
                        s = unk;
                    }
                    word.push_back(s);
                    sentence.chars.push_back(s);
                }
                word.push_back(eos);
                sentence.chars.push_back(space);
                sentence.words.push_back(word);
            }
            sentence.chars.push_back(eos);
            sentence.words.push_back(get_eos_obs());
            return sentence;
        }

        std::tuple<instances, instances>
        read(std::string path,
             std::set<size_t> train_idx,
             std::set<size_t> test_idx) {

            // Read train indices
            std::tuple<instances, instances> ret;
            std::get<0>(ret) = read(path, train_idx);

            // Freeze the symbol table
            symtab.freeze();
            frozen = true;

            std::get<1>(ret) = read(path, test_idx);
            return ret;
        }

        std::string decode(std::vector<size_t> word) const {
            std::string ret;
            for(auto it = word.begin(); it != word.end(); ++it) {
                ret = ret + symtab.val(*it);
            }
            return ret;
        }

        std::string get_instance_chars_string(instance i) const {
            return decode(i.chars);
        }

        std::string get_instance_words_string(instance i) const {
            std::string ret;
            for(auto it = i.words.begin(); it != i.words.end(); ++it) {
                ret = ret + " " + decode(*it);
            }
            return ret;
        }

        std::vector<instance>
        read(std::string path,
             std::set<size_t> include = std::set<size_t>()) {
            std::vector<instance> ret;
            std::string line;
            std::ifstream infile;

            bool filter = (include.size() > 0);
            LOG(INFO) << "filter? " << filter
                      << " include.size() = " << include.size();

            infile.open(path);
            if(!infile) {
                LOG(FATAL) << "Error reading path: `" << path << "'";
                throw std::system_error(EIO, std::generic_category());
            }

            std::set<size_t> unique_syms;

            instance sentence;
            size_t nwords { 0 };
            size_t ntags  { 0 };
            sentence.chars.push_back(bos);

            size_t tot_n_words {0};
            size_t tot_n_tags  {0};

            size_t n_full {0};
            size_t n_semi {0};
            size_t n_none {0};

            size_t n_unk  {0};

            size_t idx {0};
            int line_idx {-1};

            while (std::getline(infile, line)) {
                ++line_idx;
                std::vector<std::string> toks;
                boost::split(toks, line, boost::is_any_of(" \n\t"));
                if(toks.size() != 2) { // end of sentence
                    if(sentence.tags.size() < 1) {
                        throw std::runtime_error("pushing empty sentence");
                    }
                    sentence.chars.push_back(eos);

                    // Add an EOS word:
                    sentence.words.push_back( get_eos_obs() );

                    // Make sure total len makes sense:
                    size_t tot_len = 0;
                    for (auto l : sentence.lens) {
                        CHECK(l > 0) << "bad len";
                        tot_len += l;
                    }
                    CHECK(tot_len == nwords) << nwords << " != " << tot_len
                                             << "; line = " << line_idx;

                    // Check last word isn't empty
                    CHECK(sentence.words.back().size() > 2) << "empty last word";
                    auto last_word = sentence.words.back();

                    if(nwords == ntags) {
                        sentence.obs = Annotation::FULL;
                        ++ n_full;
                    } else if(ntags > 0) {
                        sentence.obs = Annotation::SEMI;
                        ++ n_semi;
                    } else {
                        sentence.obs = Annotation::NONE;
                        ++ n_none;
                    }

                    if((filter && include.count(idx) > 0) || (!filter)) {
                        ret.push_back(sentence); // push back a copy
                        tot_n_words += nwords;
                        tot_n_tags += ntags;
                    }

                    nwords = 0;
                    ntags = 0;
                    sentence.clear();
                    sentence.chars.push_back(bos);

                    // Increment sentence index
                    ++ idx;

                    continue;
                }
                else if (toks.size() == 2) {
                    ++ nwords;

                    std::string obs(toks[0]);
                    std::string raw_tag(toks[1]);

                    if (!(raw_tag == unk_tag)) {
                        ++ ntags;
                    }

                    CHECK(obs != "") << "empty observation for line: " << line;

                    // split tag into parts (takes strings)
                    auto other_tag_str = tagtab.val(other_tag);
                    auto parts         = split_tag(raw_tag, other_tag_str);

                    std::string tag_type(parts[0]);
                    std::string tag(parts[1]);

                    // split the input string into UTF8 codes
                    std::vector<std::string> chars = split_utf8_word(obs);

                    // the "other" tag will be of this type
                    if(tag_type == "B") {
                        if(sentence.words.size() > 0) {
                            sentence.chars.push_back( space );
                        }
                        sentence.lens.push_back( 1 );
                        sentence.tags.push_back( tagtab.get_or_add_key(tag) );
                    }

                    // start every word seq with a distinguished
                    // beginning-of-sequence symbol
                    std::vector<k_type> w { bos };
                    sentence.words.push_back( w );

                    // current word and its length
                    std::vector<k_type>& word = sentence.words.back();
                    size_t& len               = sentence.lens.back();

                    // if continuing a phrase, increment the sequence length
                    if(tag_type == "I") {
                        ++len;
                        sentence.chars.push_back( space );
                    }

                    // add all the characters to the word
                    size_t i = 0;
                    for (std::string c : chars) {
                        k_type s;

                        if(frozen) {
                            if(symtab.has_key(c)) {
                                s = symtab.key(c);
                            } else {
                                n_unk++;
                                s = unk;
                            }
                        } else {
                            s = symtab.get_or_add_key(c);
                        }

                        unique_syms.insert(s);
                        word.push_back(s);
                        sentence.chars.push_back(s);
                    }

                    // end every word with a distinguished
                    // end-of-sequence symbol
                    word.push_back(eos);

                    // update the context map if necessary
                    add_to_context_map(obs, chars, word);
                } else {
                    throw std::runtime_error("unrecognized data format");
                }
            }

            infile.close();

            //CHECK(tot_n_words > 0);
            //CHECK(tot_n_tags > 0);

            LOG(INFO) << "n_unique_sym = " << unique_syms.size();
            LOG(INFO) << "n_words = " << tot_n_words << " n_tags = " << tot_n_tags;
            LOG(INFO) << "n_full = " << n_full << " n_semi = " << n_semi
                      << " n_none = " << n_none;

            return ret;
        }

        void add_to_context_map(std::string obs, std::vector<std::string> chars,
                                std::vector<size_t> encoded) {
            if (context_map.count(encoded) > 0) {
                return;
            }
            std::string key;
            if (is_number(obs)) {
                key = "<NUM>";
            } else {
                std::transform(obs.begin(), obs.end(), std::back_inserter(key),
                               ::toupper);
            }
            auto val = vocab.get_or_add_key(key);
            context_map[encoded] = val;
        }

        // This must be called prior to doing inference, since the
        // context map is initialized with the tags below.
        void freeze() {
            symtab.freeze();
            tagtab.freeze();
            frozen = true;
        }

        void add_tags_to_context_map() {
            // Add all tags to the context map
            for (auto tag : tagtab.get_key_set()) {
                LOG(INFO) << "tag: " << tag;
                if (tag != other_tag) {
                    auto tagstr = get_tag_context_string(tag);
                    LOG(INFO) << "tagstr: " << tagstr;
                    if (vocab.has_key(tagstr)) {
                        throw std::logic_error("tagstr already in vocab");
                    }
                    auto val = vocab.add_key(tagstr);
                    //auto val = vocab.key(tagstr);
                    LOG(INFO) << "val: " << val;
                    auto tagvec = make_tag_context_key(tag);
                    if (context_tag_keys.count(tag) > 0) {
                        throw std::logic_error("tag context key already in map");
                    }
                    context_tag_keys[tag] = tagvec;
                    if (context_map.count(tagvec) > 0) {
                        throw std::logic_error("tag already in context map");
                    }
                    context_map[tagvec] = val;
                }
            }
        }

        void finalize() {
            freeze();
            add_tags_to_context_map();

            // Add BOS to the context map
            auto bosvec = get_bos_obs();
            std::string bosstr {"<BOS>"};
            auto val = vocab.add_key(bosstr);
            if (context_map.count(bosvec)) {
                throw std::logic_error("bos already in context map");
            }
            context_map[bosvec] = val;

            // Stats on the lookup tables for context representation
            LOG(INFO) << vocab.size() << " unique words in the vocabulary";
            LOG(INFO) << context_map.size() << " keys in the context map";
        }

        void log_instance(const instance& i) {
            LOG(INFO) << i.words.size() << " words";
            LOG(INFO) << i.lens.size()  << " lens";
            LOG(INFO) << i.chars.size() << " chars";
            std::vector<std::string> chars;
            for(auto sym : i.chars) {
                chars.push_back(symtab.val(sym));
            }
            CHECK(chars.size() > 0) << "empty instance";
            std::ostringstream oss;
            std::copy(chars.begin(),
                      chars.end(),
                      std::ostream_iterator<std::string>(oss, ""));
            auto it = i.words.begin();
            for (auto j=0; j < i.tags.size(); ++j) {
                auto tag = i.tags[j];
                auto len = i.lens[j];

                auto phrase = nn::join(it,it+len,bos,space,eos);

                chars.clear();
                for(auto sym : phrase) {
                    chars.push_back(symtab.val(sym));
                }
                CHECK(chars.size() > 0) << "empty instance";
                std::ostringstream oss;
                std::copy(chars.begin(),
                          chars.end(),
                          std::ostream_iterator<std::string>(oss, ""));
                it += len;
            }
        }
    };
}

#endif
