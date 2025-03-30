#include <expat.h>
#include <fstream>
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cinttypes>
#include <unordered_set>
#include <iomanip>
#include <limits>
#include <cctype>

using namespace std;

constexpr unsigned myhash(const char* str, int h = 0) {
    return !str[h] ? 5381 : (myhash(str, h + 1) * 33) ^ str[h];
}

struct ArticleInfo {
    string filename;
    uint64_t start;
    uint64_t end;
};

void showProgress(float progress, const string& phase) {
    const int barWidth = 50;
    cout << "\r" << phase << ": [";
    int pos = barWidth * progress;
    for (int i = 0; i < barWidth; ++i) {
        cout << (i <= pos ? "#" : " ");
    }
    cout << "] " << fixed << setprecision(1) << (progress * 100.0) << "%";
    cout.flush();
}

struct ParserContext {
    ArticleInfo currentArticle;
    string currentKey;
    string currentData;
    vector<string> authors;
    string originalTitle;

    string currentFilename;
    string baseName;

    ofstream keyOutput;
    ofstream titleOutput;
    ofstream authorOutput;

    int fileCounter[3] = { 0 };
    long currentBytes[3] = { 0 };
    const long MAX_SIZE = 10 * 1024 * 1024;

    XML_Parser parser;
    streamsize totalSize = 0;
    streamsize processedBytes = 0;
};

bool isTargetTag(const char* name) {
    static const unordered_set<unsigned> tagHashes = {
        myhash("article"), myhash("inproceedings"), myhash("proceedings"),
        myhash("book"), myhash("incollection"), myhash("phdthesis"),
        myhash("mastersthesis"), myhash("www"), myhash("data")
    };
    return tagHashes.count(myhash(name)) > 0;
}

string normalizeText(string input) {
    string output;

    // 转换为小写
    transform(input.begin(), input.end(), input.begin(), ::tolower);

    // 替换标点为空格
    for (char& c : input) {
        if (ispunct(static_cast<unsigned char>(c))) {
            c = ' ';
        }
    }

    // 合并连续空格
    bool prevSpace = false;
    for (char c : input) {
        if (c == ' ') {
            if (!prevSpace) {
                output += ' ';
                prevSpace = true;
            }
        }
        else {
            output += c;
            prevSpace = false;
        }
    }

    // 去除首尾空格
    size_t start = output.find_first_not_of(" ");
    if (start != string::npos) {
        size_t end = output.find_last_not_of(" ");
        output = output.substr(start, end - start + 1);
    }
    else {
        output.clear();
    }

    return output;
}

string filterEnglish(const string& input) {
    string result;
    // 过滤非ASCII字符
    for (char c : input) {
        if (static_cast<unsigned char>(c) <= 0x7F) {
            result += c;
        }
    }
    // 标准化处理
    return normalizeText(result);
}

void XMLCALL startHandler(void* userData, const XML_Char* name, const XML_Char** attrs) {
    ParserContext* ctx = static_cast<ParserContext*>(userData);

    if (isTargetTag(name)) {
        ctx->currentArticle.start = XML_GetCurrentByteIndex(ctx->parser);
        ctx->currentKey.clear();
        for (int i = 0; attrs[i]; i += 2) {
            if (strcmp(attrs[i], "key") == 0) {
                ctx->currentKey = attrs[i + 1];
                break;
            }
        }
    }
    ctx->currentData.clear();
}

void XMLCALL endHandler(void* userData, const XML_Char* name) {
    ParserContext* ctx = static_cast<ParserContext*>(userData);

    if (myhash(name) == myhash("title")) {
        ctx->originalTitle = ctx->currentData;
    }
    else if (myhash(name) == myhash("author")) {
        ctx->authors.push_back(ctx->currentData);
    }

    if (isTargetTag(name)) {
        ctx->currentArticle.end = XML_GetCurrentByteIndex(ctx->parser)
            + XML_GetCurrentByteCount(ctx->parser);
        ctx->currentArticle.filename = ctx->currentFilename;

        auto writeEntry = [&](ofstream& out, int index, const string& content) {
            if (ctx->currentBytes[index] + content.size() > ctx->MAX_SIZE) {
                out.close();
                string types[3] = { "key", "title", "author" };
                string filename = ctx->baseName + "_" + types[index] + "_part"
                    + to_string(++ctx->fileCounter[index]) + ".txt";
                out.open(filename);
                ctx->currentBytes[index] = 0;
            }
            out << content;
            ctx->currentBytes[index] += content.size();
            };

        // Key索引
        char keyEntry[512];
        snprintf(keyEntry, sizeof(keyEntry), "%s %s %" PRIu64 " %" PRIu64 "\n",
            ctx->currentKey.c_str(), ctx->currentArticle.filename.c_str(),
            ctx->currentArticle.start, ctx->currentArticle.end);
        writeEntry(ctx->keyOutput, 0, keyEntry);

        // 标题索引（仅存储标准化标题）
        if (!ctx->originalTitle.empty()) {
            string normalizedTitle = filterEnglish(ctx->originalTitle);
            if (!normalizedTitle.empty()) {
                string titleEntry = normalizedTitle + "|" + ctx->currentKey + "\n";
                writeEntry(ctx->titleOutput, 1, titleEntry);
            }
        }

        // 作者索引
        for (const auto& author : ctx->authors) {
            string authorEntry = author + " " + ctx->currentKey + "\n";
            writeEntry(ctx->authorOutput, 2, authorEntry);
        }

        ctx->authors.clear();
        ctx->originalTitle.clear();
    }
}

void XMLCALL charDataHandler(void* userData, const XML_Char* s, int len) {
    ParserContext* ctx = static_cast<ParserContext*>(userData);
    ctx->currentData.append(s, len);
}

void parseXML(const string& filename) {
    const int BUFFER_SIZE = 1 * 1024 * 1024;
    unique_ptr<char[]> buffer(new char[BUFFER_SIZE]);

    XML_Parser parser = XML_ParserCreate(nullptr);
    ParserContext ctx;
    ctx.parser = parser;
    ctx.currentFilename = filename;

    ifstream sizeFile(filename, ios::binary | ios::ate);
    ctx.totalSize = sizeFile.tellg();
    sizeFile.close();

    size_t lastDot = filename.find_last_of('.');
    ctx.baseName = (lastDot != string::npos) ? filename.substr(0, lastDot) : filename;

    ctx.keyOutput.open(ctx.baseName + "_key_part0.txt");
    ctx.titleOutput.open(ctx.baseName + "_title_part0.txt");
    ctx.authorOutput.open(ctx.baseName + "_author_part0.txt");

    XML_SetUserData(parser, &ctx);
    XML_SetElementHandler(parser, startHandler, endHandler);
    XML_SetCharacterDataHandler(parser, charDataHandler);

    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "无法打开文件: " << filename << endl;
        return;
    }

    bool done = false;
    while (!done) {
        file.read(buffer.get(), BUFFER_SIZE);
        streamsize len = file.gcount();

        if (file.eof()) done = true;
        if (XML_Parse(parser, buffer.get(), len, done) == XML_STATUS_ERROR) {
            cerr << "解析错误: " << XML_ErrorString(XML_GetErrorCode(parser))
                << " 偏移量: " << XML_GetCurrentByteIndex(parser) << endl;
            break;
        }

        ctx.processedBytes += len;
        showProgress(static_cast<float>(ctx.processedBytes) / ctx.totalSize, "解析进度");
    }
    cout << endl;

    XML_ParserFree(parser);
    ctx.keyOutput.close();
    ctx.titleOutput.close();
    ctx.authorOutput.close();
}

string getArticleContent(const ArticleInfo& info) {
    ifstream file(info.filename, ios::binary);
    if (!file) return "无法打开文件";

    file.seekg(info.start);
    vector<char> buffer(info.end - info.start);
    file.read(buffer.data(), buffer.size());
    return string(buffer.begin(), buffer.end());
}

void loadAllIndex(
    unordered_map<string, ArticleInfo>& keyIndex,
    unordered_map<string, string>& titleIndex,  // key: 标准化标题, value: 文章key
    unordered_multimap<string, string>& authorIndex,
    const vector<string>& filenames)
{
    int totalFiles = 0;
    int processedFiles = 0;

    for (const auto& filename : filenames) {
        size_t lastDot = filename.find_last_of('.');
        string baseName = (lastDot != string::npos) ? filename.substr(0, lastDot) : filename;

        auto countParts = [&](const string& type) {
            int part = 0;
            while (true) {
                string indexFile = baseName + "_" + type + "_part" + to_string(part++) + ".txt";
                ifstream file(indexFile);
                if (!file) break;
                totalFiles++;
            }
            };

        countParts("key");
        countParts("title");
        countParts("author");
    }

    for (const auto& filename : filenames) {
        size_t lastDot = filename.find_last_of('.');
        string baseName = (lastDot != string::npos) ? filename.substr(0, lastDot) : filename;

        auto loadIndexPart = [&](const string& type, auto& index, auto parser) {
            int part = 0;
            while (true) {
                string indexFile = baseName + "_" + type + "_part" + to_string(part++) + ".txt";
                ifstream file(indexFile);
                if (!file) break;

                string line;
                while (getline(file, line)) {
                    parser(line, index);
                }

                showProgress(static_cast<float>(++processedFiles) / totalFiles, "索引进度");
            }
            };

        loadIndexPart("key", keyIndex, [](const string& line, auto& index) {
            istringstream iss(line);
            string key, filename;
            ArticleInfo info;
            if (iss >> key >> info.filename >> info.start >> info.end) {
                index[key] = info;
            }
            });

        loadIndexPart("title", titleIndex, [](const string& line, auto& index) {
            size_t pos = line.find('|');
            if (pos != string::npos) {
                string normalizedTitle = line.substr(0, pos);
                string key = line.substr(pos + 1);
                index[normalizedTitle] = key;
            }
            });

        loadIndexPart("author", authorIndex, [](const string& line, auto& index) {
            size_t pos = line.find_last_of(' ');
            if (pos != string::npos) {
                string author = line.substr(0, pos);
                string key = line.substr(pos + 1);
                index.insert({ author, key });
            }
            });
    }
    cout << endl;
}

int main() {
    vector<string> xmlFiles = { "split1.xml", "split2.xml", "split3.xml" };

    /*for (const auto& file : xmlFiles) {
        cout << "正在处理文件: " << file << endl;
        parseXML(file);
    }*///如果多次运行解析一次即可，第二次运行可将此段注释掉

    cout << "\n正在加载索引..." << endl;
    unordered_map<string, ArticleInfo> keyIndex;
    unordered_map<string, string> titleIndex;  // key: 标准化标题, value: 文章key
    unordered_multimap<string, string> authorIndex;
    loadAllIndex(keyIndex, titleIndex, authorIndex, xmlFiles);

    string query;
    while (true) {
        cout << "\n请选择查询方式:"
            << "\n1. 按标题搜索"
            << "\n2. 按作者搜索"
            << "\n3. 退出"
            << "\n请输入选项: ";

        int choice;
        while (true) {
            cin >> choice;
            if (cin.fail() || choice < 1 || choice > 3) {
                cin.clear();
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                cout << "输入错误，请重新输入（1-3）: ";
            }
            else {
                cin.ignore();
                break;
            }
        }

        if (choice == 3) break;

        cout << "请输入查询内容: ";
        getline(cin, query);

        switch (choice) {
        case 1: {
            string normalizedQuery = filterEnglish(query);
            auto it = titleIndex.find(normalizedQuery);
            if (it != titleIndex.end()) {
                auto keyIt = keyIndex.find(it->second);
                if (keyIt != keyIndex.end()) {
                    cout << "\n找到匹配文章："
                        << "\n标准化标题: " << normalizedQuery
                        << "\nKey: " << it->second
                        << "\n完整内容：\n"
                        << getArticleContent(keyIt->second)
                        << "\n----------------------------------------\n";
                }
            }
            else {
                cout << "未找到标题（已执行标准化处理，实际查询条件："
                    << normalizedQuery << "）" << endl;
            }
            break;
        }
        case 2: {
            auto range = authorIndex.equal_range(query);
            if (range.first != range.second) {
                cout << "\n找到 " << distance(range.first, range.second) << " 篇相关论文：";
                for (auto it = range.first; it != range.second; ++it) {
                    auto keyIt = keyIndex.find(it->second);
                    if (keyIt != keyIndex.end()) {
                        cout << "\n----------------------------------------"
                            << "\n作者: " << it->first
                            << "\nKey: " << it->second
                            << "\n内容：\n"
                            << getArticleContent(keyIt->second)
                            << "\n----------------------------------------\n";
                    }
                }
            }
            else {
                cout << "未找到作者" << endl;
            }
            break;
        }
        }
    }

    return 0;
}