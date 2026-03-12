export class Dictionary {
  private words: Set<string> = new Set();
  private prefixes: Set<string> = new Set();
  private loaded = false;

  async load(): Promise<void> {
    if (this.loaded) return;
    const response = await fetch('/dictionary.txt');
    const text = await response.text();
    const lines = text.split('\n');
    for (const line of lines) {
      const word = line.trim().toLowerCase();
      if (word.length >= 2) {
        this.words.add(word);
        for (let i = 2; i <= word.length; i++) {
          this.prefixes.add(word.substring(0, i));
        }
      }
    }
    this.loaded = true;
  }

  isLoaded(): boolean {
    return this.loaded;
  }

  hasWord(word: string): boolean {
    return this.words.has(word.toLowerCase());
  }

  hasPrefix(prefix: string): boolean {
    return this.prefixes.has(prefix.toLowerCase());
  }
}

// Singleton
export const dictionary = new Dictionary();
