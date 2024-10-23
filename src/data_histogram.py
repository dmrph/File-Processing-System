import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv('./word_frequencies.txt')

# sort data by frequency in descending order and select the top 50 words
df_top50 = df.sort_values(by='frequency', ascending=False).head(50)

# plot the top 50 words and their frequencies
plt.figure(figsize=(10, 8))
plt.barh(df_top50['word'], df_top50['frequency'], color='skyblue')
plt.xlabel('Frequency')
plt.ylabel('Word')
plt.title('Top 50 Most Frequent Words')
plt.gca().invert_yaxis() 
plt.tight_layout()
plt.show()
