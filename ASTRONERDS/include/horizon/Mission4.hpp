#pragma once
#include <array>

namespace horizon {
struct Mission4Question {
    const char* prompt;
    std::array<const char*,4> choices;
    int correct;
};

struct Mission4 {
    static constexpr int question_count = 9;
    static constexpr std::array<const char*,4> passage_titles{{"EXPLORING MARS","THE RED PLANET","ANCIENT LIFE","SIZE AND DISTANCE"}};
    static constexpr std::array<const char*,4> passages{{
        "Mars is one of the most explored bodies in our solar system, and it's the only planet where we've sent rovers to roam the alien landscape. NASA missions have found lots of evidence that Mars was much wetter and warmer, with a thicker atmosphere, billions of years ago.",
        "Mars was named by the ancient Romans for their god of war because its reddish color was reminiscent of blood. Other civilizations also named the planet for this attribute: the Egyptians called it \"Her Desher,\" meaning \"the red one.\" Even today, it is frequently called the \"Red Planet\" because iron minerals in the Martian dirt oxidize, or rust, causing the surface to look red.",
        "Scientists don't expect to find living things currently thriving on Mars. Instead, they're looking for signs of life that existed long ago, when Mars was warmer and covered with water.",
        "Size and Distance\n\nWith a radius of 2,106 miles (3,390 kilometers), Mars is about half the size of Earth. If Earth were the size of a nickel, Mars would be about as big as a raspberry.\n\nFrom an average distance of 142 million miles (228 million kilometers), Mars is 1.5 astronomical units away from the Sun. One astronomical unit (AU) is the distance from the Sun to Earth. From this distance, it takes sunlight 13 minutes to travel from the Sun to Mars."
    }};
    static constexpr std::array<int,question_count> question_passages{{0,0,1,1,1,2,2,3,3}};
    static constexpr std::array<Mission4Question,question_count> questions{{
        {"Why is Mars one of the most explored planets in our solar system?",{{"Because it is the closest planet to Earth.","Because NASA has sent rovers to explore its surface.","Because humans already live there.","Because it has oceans today."}},1},
        {"What have NASA missions discovered about ancient Mars?",{{"It was covered in forests.","It had a thicker atmosphere and more water.","It had large cities.","It was hotter than the Sun."}},1},
        {"Why did the ancient Romans name Mars after their god of war?",{{"Because of its blue atmosphere.","Because of its strong gravity.","Because its red color reminded them of blood.","Because it was the largest planet."}},2},
        {"What does the Egyptian name Her Desher mean?",{{"The Red One.","The Bright Star.","The God of War.","The Burning Planet."}},0},
        {"What causes Mars to appear red?",{{"Its active volcanoes.","Iron minerals in its soil oxidizing (rusting).","Red oceans beneath the surface.","Reflections from the Sun."}},1},
        {"What are scientists mainly searching for on Mars?",{{"Intelligent alien civilizations.","Living dinosaurs.","Evidence of ancient life.","Modern human settlements."}},2},
        {"Why may ancient Mars have supported life?",{{"It had warmer conditions and liquid water.","It had oxygen-rich forests.","It had modern cities.","It was closer to the Sun than Earth."}},0},
        {"What is the average distance between Mars and the Sun?",{{"93 million miles (150 million km).","142 million miles (228 million km).","67 million miles (108 million km).","484 million miles (779 million km)."}},1},
        {"How long does sunlight take to travel from the Sun to Mars?",{{"8 minutes.","5 minutes.","13 minutes.","20 minutes."}},2}
    }};
};
}
