#pragma once

#include <cpr/cpr.h>

#include <format>
#include <future>
#include <string>

struct MantellaServerInterface {
    const std::string addMessageRoute = "add_message";

    int port;
    std::string baseUrl = "http://localhost";
    int timeout = 3000;  

    /**
     * @brief Asynchronously posts `jsonBody` to the Mantella server at `route`
     * @return A std::future<cpr::Response> which you can wait on or chain
     */
    std::future<cpr::Response> PostToMantellaServerAsync(const std::string& route, const std::string& jsonBody) {
        // Create a promise/future pair
        std::promise<cpr::Response> promise;
        auto future = promise.get_future();

        // Construct the URL
        std::string url = baseUrl + ":" + std::to_string(port) + "/" + route;

        // Launch the async call using cpr::PostCallback
        cpr::PostCallback(
            // The lambda will fulfill the promise once the request finishes
            [p = std::move(promise)](cpr::Response r) mutable { p.set_value(std::move(r)); }, cpr::Url{url},
            cpr::ConnectTimeout{timeout},
            // Add any auth/headers as needed
            cpr::Authentication{"user", "pass", cpr::AuthMode::BASIC},
            cpr::Header{{"Content-Type", "application/json"}}, cpr::Header{{"Accept", "application/json"}},
            cpr::Body{jsonBody});

        // Return the future that will eventually hold the HTTP response
        return future;
    }

    /**
     * @brief Inserts a chat message into Mantella. Returns a future to chain or wait upon.
     * @param msg The chat message
     * @param characterName The name of the character sending the message
     * @return A std::future<cpr::Response>
     */
    std::future<cpr::Response> AddMessageToMantellaAsync(const std::string& msg, const std::string& characterName) {
        // Create the JSON body
        auto args = std::format(R"({{"message": "{}", "characterName": "{}"}})", msg, characterName);

        // Send the request asynchronously
        return PostToMantellaServerAsync(addMessageRoute, args);
    }
};
