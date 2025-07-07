# Vkusvill Availability Parser

This project includes a simple Selenium script that gathers availability information for Vkusvill products.

## Requirements

- Python 3.8 or later
- Google Chrome or Chromium browser installed on your system
- Chrome WebDriver installed on your system (it will be downloaded automatically if not found)

## Installation

1. (Optional) Create and activate a virtual environment:

```bash
python3 -m venv venv
source venv/bin/activate
```

2. Install Python dependencies:

```bash
pip install -r requirements.txt
```

## Usage

1. Add product URLs to `urls.txt`, one URL per line.
2. (Optional) specify custom paths to Chrome and ChromeDriver if they are not on your `PATH`. If no ChromeDriver is provided, it will be downloaded automatically:

```bash
export CHROME_BINARY=/path/to/chrome
export CHROMEDRIVER_PATH=/path/to/chromedriver
```

3. Run the script:

```bash
python parse_vkusvill_selenium.py
```

The script will open each URL in a headless Chrome session, gather availability information for each store, and print the results in the terminal.

## Files

- `parse_vkusvill_selenium.py` – the main Selenium script.
- `urls.txt` – text file with the list of URLs to process.

