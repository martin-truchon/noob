int char_count(char *string, int string_size, char search)
{
	int count = 0;
	for (int i = 0; i < string_size; i++) {
		if (string[i] == search)
			count++;
	}

	return count;
}
