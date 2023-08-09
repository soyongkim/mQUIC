#include <iostream>
#include <fstream>
#include <filesystem>

int main() {
    int size = 8000000;
    std::string sizeText = std::to_string(size);

    std::ofstream output;
    std::ifstream readHeader;

    output.open("index.html");
    readHeader.open("./header.txt");

    if(readHeader.is_open()) {
        std::string str;
        std::string::size_type pos;
        while(!readHeader.eof()) {
            getline(readHeader, str);

            pos = 0;
            // 100단위로 작성
            while((pos = str.find("XXX", pos)) != std::string::npos) {
                str.replace(pos, sizeText.length(), sizeText);
                pos += 4;
            } 
            
            str += "\n";
            output.write(str.c_str(), str.size());
        }
    }

    for(int i=0; i<size; i++) {
        output.write("M", 1);
    }

    output.seekp(0, std::ios::end);
    std::cout << "File Size: " << output.tellp() << std::endl;

    output.close();
    readHeader.close();

    return 0;
}