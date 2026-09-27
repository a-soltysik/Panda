#include "FunctionName.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <panda/Logger.hpp>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>

namespace
{
struct Example
{
    std::string_view signature;
    std::string_view display;
};

template <typename Value>
class FunctionNameProbe final
{
public:
    static auto method() -> std::source_location
    {
        return std::source_location::current();
    }

    static auto lambda() -> std::source_location
    {
        return std::invoke([] {
            return std::source_location::current();
        });
    }
};
}

TEST(FunctionName, HandlesCompilerSignatures)
{
    static constexpr std::array examples {
        Example {.signature = "static std::expected<panda::tools::Window, panda::tools::WindowError> "
                              "panda::tools::Window::create(glm::uvec2, const char*)",                 .display = "Window::create"           },
        Example {.signature = "std::expected<void, panda::Error> panda::Application::run()",
                 .display = "Application::run"                                                                                                                },
        Example {.signature = "T panda::Box<T>::get(U) const [with T = std::vector<int>; U = float]",
                 .display = "Box::get"                                                                                                                        },
        Example {.signature = "std::vector<int> panda::Box<std::vector<int>>::get<double>(double) const",
                 .display = "Box::get"                                                                                                                        },
        Example {.signature = "public: int __cdecl panda::Box<int>::get<double>(double) const",                         .display = "Box::get"                 },
        Example {.signature = "panda::Box<T>::operator std::vector<T>() const",                                         .display = "operator std::vector<T>"  },
        Example {.signature = "int panda::Box<T>::operator()(int) const",                                               .display = "operator()"               },
        Example {.signature = "std::ostream& panda::Box<T>::operator<<(int)",                                           .display = "operator<<"               },
        Example {.signature = "auto panda::Box<T>::operator<=>(const Box&) const",                                      .display = "operator<=>"              },
        Example {.signature = "panda::Box<int>::get()::<lambda(auto:1)> [with auto:1 = int]",                           .display = "<lambda>"                 },
        Example {.signature = "void __cdecl panda::Box<int>::get::<lambda_1>::operator ()(int) const",
                 .display = "<lambda>"                                                                                                                        },
        Example {.signature = "auto panda::Box<int>::get()::(anonymous class)::operator()(int) const",
                 .display = "<lambda>"                                                                                                                        },
        Example {.signature = "void panda::Box<main()::<lambda()>>::get()",                                             .display = "<lambda>"                 },
        Example {.signature = "auto probe::lambda()::(lambda)::operator()() const",                                     .display = "<lambda>"                 },
        Example {.signature = "int panda::run(int)",                                                                    .display = "panda::run"               },
        Example {.signature = "void run() noexcept(true)",                                                              .display = "run"                      },
        Example {.signature = "bool panda::operator<(const Value&, const Value&)",                                      .display = "operator<"                },
        Example {.signature = "int *panda::get()",                                                                      .display = "panda::get"               },
        Example {
                 .signature =
                "std::source_location (anonymous namespace)::FunctionNameProbe<std::array<unsigned int, 4>>::method()", .display = "FunctionNameProbe::method"},
        Example {.signature = "void panda::(anonymous namespace)::Window::create()",                                    .display = "Window::create"           },
        Example {.signature = "void (anonymous namespace)::run()",                                                      .display = "run"                      },
        Example {.signature = "",                                                                                       .display = "?"                        },
    };
    for (const auto& example : examples)
    {
        SCOPED_TRACE(example.signature);
        const panda::log::detail::FunctionName name {example.signature};
        EXPECT_EQ(name.view(), example.display);
    }
}

TEST(FunctionName, RecognizesRealMethodsAndLambdas)
{
    const auto method {FunctionNameProbe<std::array<std::uint32_t, 4>>::method()};
    const auto lambda {FunctionNameProbe<std::array<std::uint32_t, 4>>::lambda()};
    const panda::log::detail::FunctionName methodName {method.function_name()};
    const panda::log::detail::FunctionName lambdaName {lambda.function_name()};
    EXPECT_EQ(methodName.view(), "FunctionNameProbe::method");
    EXPECT_EQ(lambdaName.view(), "<lambda>");

    std::ostringstream stream;
    panda::log::Logger logger {false};
    std::ignore = logger.addSink(std::make_unique<panda::log::StreamSink>(stream));
    logger.write(panda::log::Level::Info, "test", method);
    EXPECT_TRUE(stream.str().contains(" (FunctionNameProbe::method): test"));
    EXPECT_TRUE(std::string_view {method.function_name()}.contains("FunctionNameProbe"));
}

TEST(FunctionName, BoundsOutput)
{
    const auto signature {"void panda::" + std::string(256, 'x') + "()"};
    const panda::log::detail::FunctionName name {signature};
    EXPECT_EQ(name.view().size(), 128);
    EXPECT_TRUE(name.view().ends_with("..."));
}
