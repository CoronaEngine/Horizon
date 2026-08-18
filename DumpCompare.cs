using System;
using System.IO;

public static class DumpCompare
{
    public static string Run(string a, string b)
    {
        byte[] da = File.ReadAllBytes(a);
        byte[] db = File.ReadAllBytes(b);
        if (da.Length != db.Length)
        {
            return string.Format("size mismatch: {0} vs {1}", da.Length, db.Length);
        }

        long channels = da.Length / 2;
        long diffChannels = 0;
        long diffPixels = 0;
        double maxDelta = 0.0;
        long firstDiff = -1;

        for (long p = 0; p < channels; p += 4)
        {
            bool pixelDiff = false;
            for (int c = 0; c < 4 && p + c < channels; c++)
            {
                long i = (p + c) * 2;
                ushort ha = (ushort)(da[i] | (da[i + 1] << 8));
                ushort hb = (ushort)(db[i] | (db[i + 1] << 8));
                if (ha != hb)
                {
                    diffChannels++;
                    pixelDiff = true;
                    if (firstDiff < 0)
                    {
                        firstDiff = p / 4;
                    }
                    double fa = HalfToFloat(ha);
                    double fb = HalfToFloat(hb);
                    double d = Math.Abs(fa - fb);
                    if (d > maxDelta)
                    {
                        maxDelta = d;
                    }
                }
            }
            if (pixelDiff)
            {
                diffPixels++;
            }
        }

        long pixels = channels / 4;
        return string.Format(
            "pixels={0} diffPixels={1} ({2:F8}%) diffChannels={3} maxDelta={4:G6} firstDiffPixel={5}",
            pixels, diffPixels, 100.0 * diffPixels / pixels, diffChannels, maxDelta, firstDiff);
    }

    static double HalfToFloat(ushort h)
    {
        int sign = (h >> 15) & 0x1;
        int exp = (h >> 10) & 0x1F;
        int mant = h & 0x3FF;
        double value;
        if (exp == 0)
        {
            value = mant * Math.Pow(2, -24);
        }
        else if (exp == 31)
        {
            value = mant == 0 ? double.PositiveInfinity : double.NaN;
        }
        else
        {
            value = (1.0 + mant / 1024.0) * Math.Pow(2, exp - 15);
        }
        return sign == 1 ? -value : value;
    }
}
