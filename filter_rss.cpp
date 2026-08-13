#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <regex>
#include <curl/curl.h>
#include <pugixml.hpp>

// Source RSS Feed URL
const std::string SOURCE_FEED_URL = "https://openrss.org/feed/truthsocial.com/@realDonaldTrump";

// Helper function for cURL to save HTTP response to string
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
    output->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Fetch raw RSS XML using libcurl
std::string fetch_rss(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "C++ RSS Filter Bot");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    return response;
}

// Check for <img> or image enclosures
bool has_image(const std::string& content, const pugi::xml_node& item) {
    std::regex img_regex(R"(<img\b[^>]*>)", std::regex_constants::icase);
    if (std::regex_search(content, img_regex)) return true;

    for (pugi::xml_node child : item.children("enclosure")) {
        std::string type = child.attribute("type").as_string();
        if (type.rfind("image/", 0) == 0) return true;
    }
    return false;
}

// Check for <video>, <iframe> (embedded video), or video enclosures
bool has_video(const std::string& content, const pugi::xml_node& item) {
    std::regex video_regex(R"(<(video|iframe)\b[^>]*>)", std::regex_constants::icase);
    if (std::regex_search(content, video_regex)) return true;

    for (pugi::xml_node child : item.children("enclosure")) {
        std::string type = child.attribute("type").as_string();
        if (type.rfind("video/", 0) == 0) return true;
    }
    return false;
}

// Check if > 50% of letters in text are uppercase
bool is_mostly_caps(const std::string& raw_content) {
    // Strip HTML tags
    std::regex tag_regex(R"(<[^>]*>)");
    std::string text = std::regex_replace(raw_content, tag_regex, "");

    int letter_count = 0;
    int upper_count = 0;

    for (unsigned char c : text) {
        if (std::isalpha(c)) {
            letter_count++;
            if (std::isupper(c)) {
                upper_count++;
            }
        }
    }

    if (letter_count == 0) return false;
    return (static_cast<double>(upper_count) / letter_count) > 0.5;
}

int main() {
    std::string raw_xml = fetch_rss(SOURCE_FEED_URL);
    if (raw_xml.empty()) {
        std::cerr << "Failed to fetch feed." << std::endl;
        return 1;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(raw_xml.c_str());
    if (!result) {
        std::cerr << "XML Parsing Error: " << result.description() << std::endl;
        return 1;
    }

    pugi::xml_node channel = doc.child("rss").child("channel");
    if (!channel) {
        std::cerr << "Invalid RSS feed format." << std::endl;
        return 1;
    }

    std::vector<pugi::xml_node> items_to_remove;
    int total_items = 0;
    int kept_items = 0;

    for (pugi::xml_node item : channel.children("item")) {
        total_items++;
        std::string title = item.child_value("title");
        std::string description = item.child_value("description");
        std::string content_encoded = item.child_value("content:encoded");

        std::string full_text = title + " " + description + " " + content_encoded;

        bool keep = has_image(full_text, item) || 
                    has_video(full_text, item) || 
                    is_mostly_caps(full_text);

        if (!keep) {
            items_to_remove.push_back(item);
        } else {
            kept_items++;
        }
    }

    // Remove nodes that didn't pass the checks
    for (auto& item : items_to_remove) {
        channel.remove_child(item);
    }

    // Save filtered RSS back to file
    doc.save_file("rss.xml");
    std::cout << "Filtered feed updated! Kept " << kept_items << " of " << total_items << " items." << std::endl;

    return 0;
}