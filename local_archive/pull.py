import requests
from bs4 import BeautifulSoup

for i in range(4):
    i = i+1
    url = f'https://blog.cloudflare.com/how-to-execute-an-object-file-part-{i}/'
    response = requests.get(url)
    if response.status_code == 200:
        soup = BeautifulSoup(response.content, 'html.parser')
        # Extract the main content (adjust the selector as needed)
        main_content = soup.find('article') or soup.find('div', class_='post-content')

        if main_content:
            # Save the content as an HTML file
            with open(f'blog_post{i}.html', 'w', encoding='utf-8') as file:
                file.write(str(main_content))
            print("Blog post saved as blog_post{i}.html")
        else:
            print("Main content not found.")
    else:
        print(f"Failed to retrieve the page. Status code: {response.status_code}")
