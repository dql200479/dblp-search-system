//#include <expat.h>
//#include <fstream>
//#include <iostream>
//#include <string>
//#include <vector>
//#include <cstdint>
//#include <cstring>
//#include <algorithm>
//
//class XMLSplitter {
//public:
//    XMLSplitter(const std::string& inputPath,
//        const std::string& outputPrefix,
//        uint64_t maxSizeMB = 1500)
//        : inputPath(inputPath),
//        outputPrefix(outputPrefix),
//        maxSizeBytes(maxSizeMB * 1024 * 1024),
//        currentSize(0),
//        fileCount(0),
//        depth(0),
//        inRecord(false),
//        parser(XML_ParserCreate("ISO-8859-1"))
//    {
//        if (!parser) {
//            throw std::runtime_error("Failed to create XML parser");
//        }
//        XML_SetUserData(parser, this);
//        XML_SetElementHandler(parser, startElement, endElement);
//        XML_SetCharacterDataHandler(parser, charData);
//    }
//
//    ~XMLSplitter() {
//        if (parser) {
//            XML_ParserFree(parser);
//        }
//        closeCurrentFile();
//    }
//
//    void split() {
//        std::ifstream file(inputPath, std::ios::binary);
//        if (!file.is_open()) {
//            throw std::runtime_error("Cannot open input file: " + inputPath);
//        }
//
//        const size_t bufferSize = 64 * 1024;
//        std::vector<char> buffer(bufferSize);
//
//        while (file) {
//            file.read(buffer.data(), bufferSize);
//            if (XML_Parse(parser, buffer.data(), static_cast<int>(file.gcount()), file.eof()) == XML_STATUS_ERROR) {
//                throw std::runtime_error(
//                    "XML parsing error at line " +
//                    std::to_string(XML_GetCurrentLineNumber(parser)) +
//                    ": " +
//                    XML_ErrorString(XML_GetErrorCode(parser)));
//            }
//        }
//    }
//
//private:
//    static std::string escapeXML(const std::string& data) {
//        std::string escaped;
//        escaped.reserve(data.size() * 1.1);  // Reserve 10% more space for escaped chars
//        for (char c : data) {
//            switch (c) {
//            case '&':  escaped += "&amp;";  break;
//            case '<':  escaped += "&lt;";   break;
//            case '>':  escaped += "&gt;";   break;
//            case '"':  escaped += "&quot;"; break;
//            case '\'': escaped += "&apos;"; break;
//            default:   escaped += c;        break;
//            }
//        }
//        return escaped;
//    }
//
//    static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** attrs) {
//        XMLSplitter* self = static_cast<XMLSplitter*>(userData);
//
//        if (strcmp(name, "dblp") == 0) {
//            self->dblpAttributes.clear();
//            for (int i = 0; attrs[i]; i += 2) {
//                self->dblpAttributes += " ";
//                self->dblpAttributes += attrs[i];
//                self->dblpAttributes += "=\"";
//                self->dblpAttributes += self->escapeXML(attrs[i + 1]);
//                self->dblpAttributes += "\"";
//            }
//            return;
//        }
//
//        if (!self->inRecord) {
//            self->inRecord = true;
//            self->currentElement.clear();
//            self->depth = 0;
//        }
//
//        if (self->inRecord) {
//            self->depth++;
//            self->currentElement += "<";
//            self->currentElement += name;
//
//            for (int i = 0; attrs[i]; i += 2) {
//                self->currentElement += " ";
//                self->currentElement += attrs[i];
//                self->currentElement += "=\"";
//                self->currentElement += self->escapeXML(attrs[i + 1]);
//                self->currentElement += "\"";
//            }
//            self->currentElement += ">";
//        }
//    }
//
//    static void XMLCALL endElement(void* userData, const XML_Char* name) {
//        XMLSplitter* self = static_cast<XMLSplitter*>(userData);
//
//        if (self->inRecord) {
//            self->currentElement += "</";
//            self->currentElement += name;
//            self->currentElement += ">";
//            self->depth--;
//
//            if (self->depth == 0) {
//                self->inRecord = false;
//
//                if (self->needNewFile(self->currentElement.size())) {
//                    self->closeCurrentFile();
//                    self->createNewFile();
//                }
//
//                self->writeToFile(self->currentElement);
//                self->currentElement.clear();
//            }
//        }
//    }
//
//    static void XMLCALL charData(void* userData, const XML_Char* s, int len) {
//        XMLSplitter* self = static_cast<XMLSplitter*>(userData);
//        if (self->inRecord) {
//            self->currentElement += self->escapeXML(std::string(s, len));
//        }
//    }
//
//    bool needNewFile(size_t addition) const {
//        return (currentSize + addition) > maxSizeBytes && currentSize > 0;
//    }
//
//    void createNewFile() {
//        fileCount++;
//        currentSize = 0;
//        std::string filename = outputPrefix + "_" + std::to_string(fileCount) + ".xml";
//        currentFile.open(filename, std::ios::binary);
//        if (!currentFile.is_open()) {
//            throw std::runtime_error("Cannot create output file: " + filename);
//        }
//
//        std::string header = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n<dblp"
//            + dblpAttributes + ">\n";
//        currentFile << header;
//        currentSize += header.size();
//    }
//
//    void writeToFile(const std::string& content) {
//        if (!currentFile.is_open()) {
//            createNewFile();
//        }
//
//        currentFile << content << "\n";
//        currentSize += content.size() + 1;
//    }
//
//    void closeCurrentFile() {
//        if (currentFile.is_open()) {
//            currentFile << "</dblp>";
//            currentFile.close();
//        }
//    }
//
//    std::string inputPath;
//    std::string outputPrefix;
//    uint64_t maxSizeBytes;
//    uint64_t currentSize;
//    int fileCount;
//
//    XML_Parser parser;
//    std::ofstream currentFile;
//    std::string currentElement;
//    std::string dblpAttributes;
//    int depth;
//    bool inRecord;
//};
//
//int main(int argc, char* argv[]) {
//    try {
//        std::string inputFile = "dblp.xml";
//        std::string outputPrefix = "split";
//        uint64_t maxSizeMB = 1500;
//
//        if (argc > 1) inputFile = argv[1];
//        if (argc > 2) outputPrefix = argv[2];
//        if (argc > 3) maxSizeMB = std::stoull(argv[3]);
//
//        std::cout << "Splitting XML file: " << inputFile << std::endl;
//        std::cout << "Output prefix: " << outputPrefix << std::endl;
//        std::cout << "Max size per file: " << maxSizeMB << " MB" << std::endl;
//
//        XMLSplitter splitter(inputFile, outputPrefix, maxSizeMB);
//        splitter.split();
//
//        std::cout << "XML splitting completed successfully!" << std::endl;
//    }
//    catch (const std::exception& e) {
//        std::cerr << "Error: " << e.what() << std::endl;
//        return 1;
//    }
//    return 0;
//}