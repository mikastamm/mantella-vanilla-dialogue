#pragma once

#include <cpr/cpr.h>

#include <format>
#include <string>

struct MantellaServerInterface {
    const std::string addMessageRoute = "add_message";

    int port;
    std::string baseUrl = "http://localhost";
    int timeoutMs = 3000;

    /**
     * @brief Posts `jsonBody` to the Mantella server at `route` (synchronous).
     * @return A cpr::Response containing the results of the HTTP POST.
     */
    cpr::Response PostToMantellaServer(const std::string& route, const std::string& jsonBody) {
        // Construct the URL
        std::string url = baseUrl + ":" + std::to_string(port) + "/" + route;

        // Perform the POST request synchronously
        return cpr::Post(cpr::Url{url}, cpr::ConnectTimeout{timeoutMs},
                         // Add any auth/headers as needed
                         cpr::Authentication{"user", "pass", cpr::AuthMode::BASIC},
                         cpr::Header{{"Content-Type", "application/json"}}, cpr::Header{{"Accept", "application/json"}},
                         cpr::Body{jsonBody});
    }

    /**
     * @brief Inserts a chat message into Mantella. This is a synchronous call.
     * @param msg The chat message
     * @param characterName The name of the character sending the message
     * @return cpr::Response
     */
    cpr::Response AddMessageToMantella(const std::string& msg, const std::string& characterName) {
        // Create the JSON body
        auto args = std::format(R"({{"message": "{}", "characterName": "{}"}})", msg, characterName);

        // Send the request synchronously
        return PostToMantellaServer(addMessageRoute, args);
    }
};
