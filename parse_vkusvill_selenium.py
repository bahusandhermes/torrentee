import re
import time
from typing import List, Tuple

from selenium import webdriver
from selenium.webdriver.chrome.options import Options
from selenium.webdriver.chrome.service import Service
from webdriver_manager.chrome import ChromeDriverManager
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from selenium.common.exceptions import ElementClickInterceptedException
import os
import shutil

URLS_FILE = "urls.txt"


def read_urls(path: str) -> List[str]:
    with open(path, "r", encoding="utf-8") as f:
        return [line.strip() for line in f if line.strip()]


def setup_driver() -> webdriver.Chrome:
    options = Options()
    options.add_argument("--headless=new")
    options.add_argument("--no-sandbox")
    options.add_argument("--disable-dev-shm-usage")

    chrome_binary = os.environ.get("CHROME_BINARY")
    if chrome_binary:
        options.binary_location = chrome_binary

    driver_path = os.environ.get("CHROMEDRIVER_PATH") or shutil.which("chromedriver")
    if not driver_path:
        driver_path = ChromeDriverManager().install()
    service = Service(driver_path)

    driver = webdriver.Chrome(service=service, options=options)
    return driver


def open_availability_panel(driver: webdriver.Chrome) -> None:
    """Open the availability panel if the button is present."""
    try:
        button = WebDriverWait(driver, 20).until(
            EC.element_to_be_clickable((By.CSS_SELECTOR, "button.js-vv23-product-goto-rests"))
        )
    except Exception as exc:
        raise RuntimeError("Не удалось найти кнопку просмотра остатков") from exc

    # Scroll to the button to ensure it is not covered by the header
    driver.execute_script("arguments[0].scrollIntoView({block: 'center'});", button)
    try:
        button.click()
    except ElementClickInterceptedException:
        # Some overlays (e.g. cart menu) may intercept the click
        driver.execute_script("arguments[0].click();", button)
    WebDriverWait(driver, 20).until(
        EC.presence_of_element_located((By.CSS_SELECTOR, "div.VV21_MapPanelShop__Col"))
    )


def load_all_shops(driver: webdriver.Chrome) -> None:
    prev_count = -1
    attempts = 0
    while attempts < 30:
        shops = driver.find_elements(By.CSS_SELECTOR, "div.VV21_MapPanelShop__Col")
        count = len(shops)
        if count == prev_count:
            attempts += 1
        else:
            attempts = 0
            prev_count = count
        if shops:
            driver.execute_script("arguments[0].scrollIntoView();", shops[-1])
        time.sleep(1)
        if attempts >= 3:
            break


def summarize(driver: webdriver.Chrome) -> Tuple[int, int]:
    shops = driver.find_elements(By.CSS_SELECTOR, "div.VV21_MapPanelShop__Col")
    store_count = 0
    total_qty = 0
    for shop in shops:
        try:
            qty_text = shop.find_element(By.CSS_SELECTOR, "div.Map__rests").text
            qty_match = re.search(r"\d+", qty_text)
            qty = int(qty_match.group()) if qty_match else 0
            if qty > 0:
                store_count += 1
                total_qty += qty
        except Exception:
            continue
    return store_count, total_qty


def main() -> None:
    urls = read_urls(URLS_FILE)
    driver = setup_driver()
    try:
        for url in urls:
            driver.get(url)
            try:
                open_availability_panel(driver)
                load_all_shops(driver)
                stores, qty = summarize(driver)
            except Exception as exc:
                print(f"Ошибка при обработке {url}: {exc}")
                continue
            print("товар")
            print(url)
            print("количество магазинов с ненулевым остатком")
            print(stores)
            print("количество товаров во всех магазинах")
            print(qty)
            print("-" * 30)
    finally:
        driver.quit()


if __name__ == "__main__":
    main()
