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

    // ת��ΪСд
    transform(input.begin(), input.end(), input.begin(), ::tolower);

    // �滻���Ϊ�ո�
    for (char& c : input) {
        if (ispunct(static_cast<unsigned char>(c))) {
            c = ' ';
        }
    }

    // �ϲ������ո�
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

    // ȥ����β�ո�
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
    // ���˷�ASCII�ַ�
    for (char c : input) {
        if (static_cast<unsigned char>(c) <= 0x7F) {
            result += c;
        }
    }
    // ��׼������
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

        // Key����
        char keyEntry[512];
        snprintf(keyEntry, sizeof(keyEntry), "%s %s %" PRIu64 " %" PRIu64 "\n",
            ctx->currentKey.c_str(), ctx->currentArticle.filename.c_str(),
            ctx->currentArticle.start, ctx->currentArticle.end);
        writeEntry(ctx->keyOutput, 0, keyEntry);

        // �������������洢��׼�����⣩
        if (!ctx->originalTitle.empty()) {
            string normalizedTitle = filterEnglish(ctx->originalTitle);
            if (!normalizedTitle.empty()) {
                string titleEntry = normalizedTitle + "|" + ctx->currentKey + "\n";
                writeEntry(ctx->titleOutput, 1, titleEntry);
            }
        }

        // ��������
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
        cerr << "�޷����ļ�: " << filename << endl;
        return;
    }

    bool done = false;
    while (!done) {
        file.read(buffer.get(), BUFFER_SIZE);
        streamsize len = file.gcount();

        if (file.eof()) done = true;
        if (XML_Parse(parser, buffer.get(), len, done) == XML_STATUS_ERROR) {
            cerr << "��������: " << XML_ErrorString(XML_GetErrorCode(parser))
                << " ƫ����: " << XML_GetCurrentByteIndex(parser) << endl;
            break;
        }

        ctx.processedBytes += len;
        showProgress(static_cast<float>(ctx.processedBytes) / ctx.totalSize, "��������");
    }
    cout << endl;

    XML_ParserFree(parser);
    ctx.keyOutput.close();
    ctx.titleOutput.close();
    ctx.authorOutput.close();
}

string getArticleContent(const ArticleInfo& info) {
    ifstream file(info.filename, ios::binary);
    if (!file) return "�޷����ļ�";

    file.seekg(info.start);
    vector<char> buffer(info.end - info.start);
    file.read(buffer.data(), buffer.size());
    return string(buffer.begin(), buffer.end());
}

void loadAllIndex(
    unordered_map<string, ArticleInfo>& keyIndex,
    unordered_map<string, string>& titleIndex,  // key: ��׼������, value: ����key
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

                showProgress(static_cast<float>(++processedFiles) / totalFiles, "��������");
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
        cout << "���ڴ����ļ�: " << file << endl;
        parseXML(file);
    }*///���������н���һ�μ��ɣ��ڶ������пɽ��˶�ע�͵�

    cout << "\n���ڼ�������..." << endl;
    unordered_map<string, ArticleInfo> keyIndex;
    unordered_map<string, string> titleIndex;  // key: ��׼������, value: ����key
    unordered_multimap<string, string> authorIndex;
    loadAllIndex(keyIndex, titleIndex, authorIndex, xmlFiles);

    string query;
    while (true) {
        cout << "\n��ѡ���ѯ��ʽ:"
            << "\n1. ����������"
            << "\n2. ����������"
            << "\n3. �˳�"
            << "\n������ѡ��: ";

        int choice;
        while (true) {
            cin >> choice;
            if (cin.fail() || choice < 1 || choice > 3) {
                cin.clear();
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                cout << "����������������루1-3��: ";
            }
            else {
                cin.ignore();
                break;
            }
        }

        if (choice == 3) break;

        cout << "�������ѯ����: ";
        getline(cin, query);

        switch (choice) {
        case 1: {
            string normalizedQuery = filterEnglish(query);
            auto it = titleIndex.find(normalizedQuery);
            if (it != titleIndex.end()) {
                auto keyIt = keyIndex.find(it->second);
                if (keyIt != keyIndex.end()) {
                    cout << "\n�ҵ�ƥ�����£�"
                        << "\n��׼������: " << normalizedQuery
                        << "\nKey: " << it->second
                        << "\n�������ݣ�\n"
                        << getArticleContent(keyIt->second)
                        << "\n----------------------------------------\n";
                }
            }
            else {
                cout << "δ�ҵ����⣨��ִ�б�׼������ʵ�ʲ�ѯ������"
                    << normalizedQuery << "��" << endl;
            }
            break;
        }
        case 2: {
            auto range = authorIndex.equal_range(query);
            if (range.first != range.second) {
                cout << "\n�ҵ� " << distance(range.first, range.second) << " ƪ������ģ�";
                for (auto it = range.first; it != range.second; ++it) {
                    auto keyIt = keyIndex.find(it->second);
                    if (keyIt != keyIndex.end()) {
                        cout << "\n----------------------------------------"
                            << "\n����: " << it->first
                            << "\nKey: " << it->second
                            << "\n���ݣ�\n"
                            << getArticleContent(keyIt->second)
                            << "\n----------------------------------------\n";
                    }
                }
            }
            else {
                cout << "δ�ҵ�����" << endl;
            }
            break;
        }
        }
    }

    return 0;
}