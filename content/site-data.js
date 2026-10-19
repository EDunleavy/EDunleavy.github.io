/* Edit with local-editor/editor.html or update this file. */
window.SITE_DATA = {
  "profile": {
    "name": "Eric Nicholas Dunleavy",
    "initials": "ED",
    "headline": "Finance Student · Builder · Problem Solver",
    "intro": "I study finance at the University of Oklahoma and like turning questions into useful things—from financial models to software and 3D-printed designs.",
    "about": "I grew up in Texas with a natural curiosity about how things work. I have always enjoyed taking things apart, learning what makes them tick, and building something practical with what I discover. My family—especially my older brother and younger sister—motivates me. I enjoy meeting people and learning from their experiences. I love cars (Ferrari is my favorite), OU football, hiking, fishing, hunting, travel, and time outdoors, especially in snowy places. A summer living and working in Nashville gave me another opportunity to explore somewhere new.",
    "email": "eric_dunleavy@yahoo.com",
    "location": "Oklahoma · Originally from Texas",
    "linkedin": "",
    "github": "https://github.com/EDunleavy"
  },
  "projects": [
    {
      "id": "obd-2-port-dashboard",
      "title": "OBD 2 Port Dashboard",
      "category": "Automotive",
      "status": "Work in progress",
      "summary": "Project details coming soon.",
      "details": "Project description, features, and build notes will be added here.",
      "progress": null,
      "updated": "",
      "updates": []
    },
    {
      "id": "loan-payment-calculator",
      "title": "Loan Payment & Extra-Payment Calculator",
      "category": "Finance · Excel",
      "status": "Completed",
      "summary": "An interactive Excel loan amortization model that compares payment schedules and shows the impact of additional principal payments.",
      "details": "I developed an interactive Excel loan amortization calculator to calculate monthly payments, total interest, and repayment schedules. Adjustable extra-payment scenarios show how additional principal payments can reduce interest and accelerate payoff. I plan to use this project log to document further enhancements, examples, and lessons learned.",
      "progress": null,
      "updated": "",
      "updates": []
    },
    {
      "id": "adsb-flight-radar",
      "title": "Live ADS-B Flight Radar",
      "category": "Coding · Aviation",
      "status": "Completed",
      "summary": "A live aircraft-tracking radar I built for my older brother.",
      "details": "A coding project that displays live ADS-B aircraft data as a flight radar. I built it for my brother. I can add technical details, screenshots, and a build log here later.",
      "progress": 100,
      "updated": "",
      "updates": []
    },
    {
      "id": "desktop-time-weather-widget",
      "title": "Desktop Time & Weather Widget",
      "category": "ESP32 · Animated desktop display",
      "status": "Completed",
      "summary": "A tiny ESP32 world where Sisyphus climbs through live weather, time, and an ever-changing journey.",
      "details": "",
      "progress": 100,
      "updated": "",
      "updates": [],
      "article": {
        "title": "Building a Tiny, Endless Journey with an ESP32",
        "paragraphs": [
          "An ESP32 and a 1.54-inch ST7789 display turn a desktop clock into a living scene: Sisyphus pushes his boulder onward while time, weather, and the night sky follow the real world."
        ]
      },
      "presentation": {
        "photos": [],
        "source": null,
        "guide": null,
        "philosophy": [
          "This project began as a compact desk display built from an ESP32 and a 1.54-inch, 240-by-240-pixel ST7789 color screen. The original idea was simply to create a small object that could display messages and change them over time without remaining connected to a computer. As the project developed, however, it became something much more personal and ambitious. Instead of behaving like a miniature information dashboard, the display became a living scene of Sisyphus pushing his boulder endlessly up a mountain. The character remains near the center of the screen while the mountain moves beneath him, creating the illusion that he is constantly climbing through an environment that never truly ends. The finished concept combines a clock, weather display, moon-phase indicator, procedural animation, persistent mileage counter, and several personal details into one small desktop companion.",
          "The hardware is intentionally straightforward. An ESP32-WROOM-32 development board provides the processing power and built-in Wi-Fi needed to run the display and retrieve live information. It is connected to the ST7789 screen through SPI, using GPIO 18 for the clock signal, GPIO 23 for display data, GPIO 17 for reset, GPIO 16 for data and command selection, GPIO 5 for chip select, and GPIO 25 for backlight control. The screen is powered from the ESP32’s 3.3-volt and ground connections. Everything is programmed through the Arduino IDE using the ESP32 board package and libraries including Adafruit GFX, Adafruit ST7789, ArduinoJson, and WiFiManager. WiFiManager allows the device to create a temporary setup network called DeskDisplay-Setup, making it possible to connect the display to a new wireless network without modifying or re-uploading its code.",
          "Once connected, the display uses the network to estimate its approximate location, retrieve current weather conditions from Open-Meteo, and synchronize the clock through an NTP time server. The upper portion of the screen shows the local time, including seconds and AM or PM, alongside the current outdoor temperature. Weather is not only reported with text; it becomes part of Sisyphus’s environment. Clear conditions produce sunlight and open skies, clouds move behind the mountains, rain falls and splashes against the rocks, lightning flashes during storms, fog softens the distant landscape, and snow gradually gathers across the ground. Smooth transitions prevent these conditions from feeling like separate screens. Instead, the weather appears to move naturally through the tiny world, connecting what happens on the display with what is happening outside.",
          "The moon is given the same attention. Its illuminated shape is calculated from the current date using the Moon’s approximate 29.53-day cycle, allowing it to pass through new, crescent, quarter, gibbous, and full phases. I wanted to include this feature because I like watches, and the moon-phase indicator is a classic watch complication. A traditional watch takes an enormous celestial cycle and represents it through a small moving image on the dial. This display follows the same idea digitally, placing the current phase above the mountain as another visual reminder of time continuing to move even while Sisyphus repeats the same task.",
          "The animation itself is generated directly by the ESP32 rather than being played from a saved video. Mathematical waves produce the uneven outlines of the mountains, textures move through the rocky ground, weather particles are calculated from frame to frame, and the character’s body follows a repeating walking cycle. Sisyphus began as a simple stick figure but developed into a more recognizable person with a head, torso, clothing, arms, legs, shading, and a posture that shows the effort of pushing. The boulder is approximately 24 pixels across and has an uneven outline instead of being a perfect circle. To prevent the flashing seen in early versions, the scene is drawn into four 240-by-60-pixel memory buffers and then transferred to the display in strips. This keeps memory use manageable while creating smoother animation at approximately ten frames per second.",
          "One of the most personal parts of the display is the mallard. My brother and my dad have always loved birds, and their interest was the inspiration for making the occasional visitor a recognizable male mallard with a green head instead of a generic bird. At random times, but no more than twice in a 24-hour period, the mallard flies into the scene and lands on the boulder. When it arrives, Sisyphus stops walking and takes a two-minute break while the bird rests beside him. During rainy weather, the mallard can shake water from its feathers before eventually flying away. It also returns during major mileage celebrations and circles the scene. The bird is more than a decorative animation; it provides Sisyphus with a brief moment of companionship and makes the endless journey feel less lonely.",
          "A permanent odometer records the total distance Sisyphus has traveled at a simulated walking speed of 3.2 miles per hour. Time spent resting with the mallard does not increase the counter. The mileage is saved to the ESP32’s non-volatile storage every fifteen minutes, preventing the journey from being erased if power is disconnected. Only the total mileage is shown. There is no progress bar, countdown, or indication of how far remains before the next transformation. This keeps the focus on the distance already traveled and allows each change to feel discovered rather than scheduled.",
          "As the mileage increases, the boulder becomes a physical record of that persistence. Scratches appear at ten miles, the surface becomes smoother at one hundred, and bronze veins emerge at five hundred. At one thousand miles it becomes bronze, followed by polished bronze at five thousand and silver at ten thousand. At twenty-five thousand miles it becomes polished silver, while fifty thousand gives it a luminous silver glow. At seventy-five thousand, gold veins begin cutting through the silver. Finally, at one hundred thousand miles, the boulder becomes permanently gold. Major transformations are recognized with an eight-second celebration containing flashes, sparkles, and the circling mallard. These rewards do not complete the journey or remove the boulder. They simply show how time and effort have changed it.",
          "Across the bottom of the screen is the line “one can imagine Sisyphus as happy.” It is adapted from the conclusion of Albert Camus’ The Myth of Sisyphus, a book I read and greatly enjoyed. Camus ends the essay with the famous statement, “One must imagine Sisyphus happy.” I changed “must” to “can” because it feels less like an instruction and more like an invitation. The task remains difficult and endless, but the changing weather, turning moon, growing mileage, evolving boulder, and visiting mallard create meaning within it. Together, these elements turn a small electronics project into a story about persistence, progress, companionship, and the possibility of finding happiness inside the climb rather than waiting for it to end."
        ],
        "features": [
          "The display connects to Wi-Fi to show the current time, outside temperature, real weather conditions, sunrise and sunset, and the active phase of the moon. The environment changes with the real world, including rain, lightning, snow, fog, moving clouds, twinkling stars, and occasional shooting stars. A mallard sometimes flies into the scene, lands on the boulder, and gives Sisyphus a short break before his climb continues.",
          "The project also keeps track of the total distance Sisyphus has traveled at a simulated walking speed of 3.2 mph. His lifetime mileage is saved even when the ESP32 loses power. As the miles accumulate, the boulder slowly develops scratches, becomes smoother, reveals bronze veins, and eventually transforms through bronze, polished silver, glowing silver, gold-veined silver, and finally permanent gold. Major milestones are marked with flashes, sparkles, a pause in the journey, and the mallard circling overhead."
        ],
        "philosophyTitle": "One Can Imagine Sisyphus Happy"
      }
    }
  ],
  "posts": [],
  "resume": {
    "summary": "University of Oklahoma finance student with internship experience in sales, customer outreach, business operations, technical field testing, and manufacturing. I bring a practical, hands-on approach to research, data analysis, problem-solving, and working across teams.",
    "education": [
      {
        "school": "University of Oklahoma · Norman, OK",
        "credential": "Bachelor of Business Administration (BBA), Finance",
        "period": "Expected May 2027",
        "details": "GPA: 3.35 · Relevant coursework: Managerial Accounting, Corporate Finance · Sooner Racing Team"
      }
    ],
    "experience": [
      {
        "role": "Sales Intern",
        "organization": "Ferguson · Nashville, TN",
        "period": "May – August 2026",
        "bullets": [
          "Rotated through warehouse operations, inside sales, and customer service to learn business operations across departments.",
          "Supported customer outreach, phone calls, and daily inside sales activities, strengthening communication and relationship-management skills.",
          "Worked with cross-functional teams to address operational challenges and resolve customer concerns."
        ]
      },
      {
        "role": "Inland Oil Rig Intern",
        "organization": "Turbo Drill Industries · Hobbs, NM",
        "period": "May – August 2024",
        "bullets": [
          "Conducted field testing of an experimental product on an active oil rig, evaluating real-world performance and reliability.",
          "Collected and analyzed field-test data to identify potential issues and support product improvements.",
          "Troubleshot technical issues on-site and collaborated on solutions to reduce testing interruptions."
        ]
      },
      {
        "role": "Shop Intern",
        "organization": "Turbo Drill Industries · Conroe, TX",
        "period": "May – August 2023",
        "bullets": [
          "Assisted with drill manufacturing and assembly while learning production processes, equipment, and quality standards.",
          "Gained hands-on exposure to heavy machinery and CNC equipment, precision manufacturing, and workplace safety.",
          "Worked with technicians on production challenges and shadowed management in planning and operations."
        ]
      }
    ],
    "skills": [
      "Customer outreach & relationship management",
      "CRM",
      "Research & data analysis",
      "Microsoft Excel",
      "Microsoft PowerPoint",
      "Cross-functional collaboration",
      "Technical troubleshooting",
      "Financial modeling"
    ],
    "certifications": [
      "Microsoft Excel Training and FMI Certification (2025)",
      "OSHA Certified (2023)",
      "CPR Certified (2022)"
    ],
    "additional": [
      "Sooner Racing Team",
      "Professional development: SolidWorks FEA and CFD"
    ],
    "projects": [
      "Interactive Excel Loan Amortization Calculator — monthly payments, total interest, repayment schedules, and extra-payment scenarios"
    ]
  }
};
