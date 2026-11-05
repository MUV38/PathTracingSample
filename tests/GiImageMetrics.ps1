Add-Type -AssemblyName System.Drawing
if (!("GiImageMetrics" -as [type]))
{
	$drawingAssemblies = @('System.Drawing')
	if ($PSVersionTable.PSEdition -eq 'Core')
	{
		$drawingAssemblies += 'System.Drawing.Primitives'
		$drawingAssemblies += [Drawing.Bitmap].Assembly.Location
		$drawingAssemblies += @(Get-ChildItem -LiteralPath $PSHOME -Filter 'System.Private.Windows*.dll' | Select-Object -ExpandProperty FullName)
	}
	Add-Type -ReferencedAssemblies $drawingAssemblies -TypeDefinition @'
using System;
using System.Drawing;
public static class GiImageMetrics
{
	static double Linear(byte value)
	{
		double s = value / 255.0;
		double mapped = s <= 0.04045 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4);
		return mapped / Math.Max(1e-6, 1 - mapped);
	}
	public static double[] Read(string path)
	{
		using (var bitmap = new Bitmap(path))
		{
			var result = new double[8];
			int count = 0;
			for (int y = 0; y < bitmap.Height; y += 4)
			{
				for (int x = (int)(bitmap.Width * .65); x < bitmap.Width; x += 4)
				{
					Color p = bitmap.GetPixel(x, y);
					result[0] += Linear(p.R);
					result[1] += Linear(p.G);
					result[2] += Linear(p.B);
					if (p.G > 150 && p.R < 10)
					{
						result[4]++;
					}
					if (p.B > 200 && p.G > 150 && p.R < 10)
					{
						result[5]++;
					}
					if (p.R > 100 && p.G > 30 && p.G < 100 && p.B < 10)
					{
						result[6]++;
					}
					if (Math.Max(p.R, Math.Max(p.G, p.B)) > 3)
					{
						result[7]++;
					}
					count++;
				}
			}
			for (int channel = 0; channel < 3; channel++)
			{
				result[channel] /= count;
			}
			result[3] = .2126 * result[0] + .7152 * result[1] + .0722 * result[2];
			return result;
		}
	}
	public static int CheckReplay(string path)
	{
		using (var bitmap = new Bitmap(path))
		{
			int continued = 0;
			for (int y = 0; y < bitmap.Height; ++y)
			{
				for (int x = (int)(bitmap.Width * .65); x < bitmap.Width; ++x)
				{
					Color p = bitmap.GetPixel(x, y);
					if (p.R != 0 || p.G != 255 || (p.B != 0 && p.B != 255))
					{
						throw new Exception("Invalid replay result at " + x + ", " + y);
					}
					if (p.B == 255)
					{
						++continued;
					}
				}
			}
			return continued;
		}
	}
	public static int MaxDifference(string referencePath, string samplePath)
	{
		using (var reference = new Bitmap(referencePath))
		using (var sample = new Bitmap(samplePath))
		{
			int error = 0;
			for (int y = 0; y < reference.Height; ++y)
			{
				for (int x = (int)(reference.Width * .65); x < reference.Width; ++x)
				{
					Color a = reference.GetPixel(x, y), b = sample.GetPixel(x, y);
					error = Math.Max(error, Math.Max(Math.Abs(a.R - b.R), Math.Max(Math.Abs(a.G - b.G), Math.Abs(a.B - b.B))));
				}
			}
			return error;
		}
	}
	public static int[] HybridWork(string path)
	{
		using (var bitmap = new Bitmap(path))
		{
			var result = new int[3];
			for (int y = 0; y < bitmap.Height; ++y)
			{
				for (int x = (int)(bitmap.Width * .65); x < bitmap.Width; ++x)
				{
					Color p = bitmap.GetPixel(x, y);
					// At 1 spp the raw R/G values represent 0, 1 or 2 checks.
					result[0] += (p.R + 64) / 128;
					result[1] += (p.G + 64) / 128;
					result[2] += p.B == 255 ? 1 : 0;
				}
			}
			return result;
		}
	}
	public static int[] ShiftQuality(string path)
	{
		using (var bitmap = new Bitmap(path))
		{
			var result = new int[6 + 2 * 256];
			for (int y = 0; y < bitmap.Height; ++y)
			{
				for (int x = (int)(bitmap.Width * .65); x < bitmap.Width; ++x)
				{
					Color p = bitmap.GetPixel(x, y);
					if (p.B % 51 != 0)
					{
						throw new Exception("Invalid shift status in " + path);
					}
					++result[p.B / 51];
					if (p.B == 204)
					{
						++result[6 + p.R];
						++result[6 + 256 + p.G];
					}
				}
			}
			return result;
		}
	}
	public static int[] ReservoirStatus(string path)
	{
		using (var bitmap = new Bitmap(path))
		{
			var result = new int[2];
			for (int y = 0; y < bitmap.Height; y += 4)
			{
				for (int x = (int)(bitmap.Width * .65); x < bitmap.Width; x += 4)
				{
					Color p = bitmap.GetPixel(x, y);
					result[0] = Math.Max(result[0], p.R);
					if (p.B == 255)
					{
						++result[1];
					}
				}
			}
			return result;
		}
	}
	static double Luminance(Color pixel)
	{
		return .2126 * Linear(pixel.R) + .7152 * Linear(pixel.G) + .0722 * Linear(pixel.B);
	}
	public static double[] TemporalCorrelation(string referencePath, string[] framePaths, int[] lags)
	{
		Point[] points;
		int pointCount = 0;
		Size size;
		using (var reference = new Bitmap(referencePath))
		{
			size = reference.Size;
			points = new Point[((size.Width + 3) / 4) * ((size.Height + 3) / 4)];
			for (int y = 0; y < size.Height; y += 4)
			{
				for (int x = (int)(size.Width * .65); x < size.Width; x += 4)
				{
					if (Luminance(reference.GetPixel(x, y)) >= .0003)
					{
						points[pointCount++] = new Point(x, y);
					}
				}
			}
		}
		if (pointCount < 100)
		{
			throw new Exception("Empty diffuse-GI reference");
		}
		var frames = new double[framePaths.Length, pointCount];
		var means = new double[pointCount];
		for (int i = 0; i < framePaths.Length; ++i)
		{
			using (var frame = new Bitmap(framePaths[i]))
			{
				if (frame.Size != size)
				{
					throw new ArgumentException("Temporal frame size differs from reference");
				}
				for (int p = 0; p < pointCount; ++p)
				{
					frames[i, p] = Luminance(frame.GetPixel(points[p].X, points[p].Y));
					means[p] += frames[i, p] / framePaths.Length;
				}
			}
		}
		var result = new double[lags.Length];
		for (int l = 0; l < lags.Length; ++l)
		{
			int lag = lags[l];
			double product = 0, firstEnergy = 0, secondEnergy = 0;
			for (int i = 0; i + lag < framePaths.Length; ++i)
			{
				for (int p = 0; p < pointCount; ++p)
				{
					double a = frames[i, p] - means[p], b = frames[i + lag, p] - means[p];
					product += a * b;
					firstEnergy += a * a;
					secondEnergy += b * b;
				}
			}
			if (firstEnergy == 0 || secondEnergy == 0)
			{
				throw new Exception("Temporal sequence has no variation");
			}
			result[l] = product / Math.Sqrt(firstEnergy * secondEnergy);
		}
		return result;
	}
	public static double[] SeedError(string referencePath, string[] samplePaths)
	{
		using (var reference = new Bitmap(referencePath))
		{
			var samples = new Bitmap[samplePaths.Length];
			try
			{
				for (int i = 0; i < samples.Length; ++i)
				{
					samples[i] = new Bitmap(samplePaths[i]);
					if (samples[i].Size != reference.Size)
					{
						throw new ArgumentException("Seed image size differs from reference");
					}
				}
				var totals = new double[4];
				int count = 0;
				for (int y = 0; y < reference.Height; y += 4)
				{
					for (int x = (int)(reference.Width * .65); x < reference.Width; x += 4)
					{
						double target = Luminance(reference.GetPixel(x, y));
						if (target < .0003)
						{
							continue;
						}
						double sum = 0, squares = 0;
						foreach (var sample in samples)
						{
							double error = Luminance(sample.GetPixel(x, y)) - target;
							sum += error;
							squares += error * error;
						}
						double mean = sum / samples.Length;
						double variance = Math.Max(0, squares - sum * mean) / (samples.Length - 1);
						totals[0] += mean;
						totals[1] += variance;
						totals[2] += mean * mean - variance / samples.Length;
						totals[3] += squares / samples.Length;
						count++;
					}
				}
				if (count < 100)
				{
					throw new Exception("Empty diffuse-GI reference");
				}
				for (int i = 0; i < totals.Length; ++i)
				{
					totals[i] /= count;
				}
				return totals;
			}
			finally
			{
				foreach (var sample in samples)
				{
					if (sample != null)
					{
						sample.Dispose();
					}
				}
			}
		}
	}
	public static int HistoryErrorMap(string referencePath, string samplePath, string guidePath, string outputPath)
	{
		using (var reference = new Bitmap(referencePath))
		using (var sample = new Bitmap(samplePath))
		using (var guide = new Bitmap(guidePath))
		using (var map = new Bitmap(reference.Width, reference.Height))
		{
			if (sample.Size != reference.Size || guide.Size != reference.Size)
			{
				throw new ArgumentException("History image size differs from reference");
			}
			int oldest = 0;
			for (int y = 0; y < map.Height; ++y)
			{
				for (int x = (int)(map.Width * .65); x < map.Width; ++x)
				{
					double target = Luminance(reference.GetPixel(x, y));
					if (target < .0003)
					{
						continue;
					}
					double error = Math.Abs(Luminance(sample.GetPixel(x, y)) - target);
					Color path = guide.GetPixel(x, y);
					int age = (int)Math.Round(path.G * (32.0 / 255));
					if ((path.B == 255) != (age > 0))
					{
						throw new Exception("History selection and age disagree");
					}
					oldest = Math.Max(oldest, age);
					map.SetPixel(x, y, Color.FromArgb((int)Math.Round(255 * Math.Min(error / .025, 1)), path.G, path.B));
				}
			}
			map.Save(outputPath, System.Drawing.Imaging.ImageFormat.Png);
			return oldest;
		}
	}
	public static double Mse(string referencePath, string samplePath)
	{
		using (var reference = new Bitmap(referencePath))
		using (var sample = new Bitmap(samplePath))
		{
			double error = 0;
			int count = 0;
			for (int y = 0; y < reference.Height; y += 4)
			{
				for (int x = (int)(reference.Width * .65); x < reference.Width; x += 4)
				{
					Color r = reference.GetPixel(x,y), s = sample.GetPixel(x,y);
					double a = Luminance(r);
					double b = Luminance(s);
					if (a < .0003)
					{
						continue;
					}
					error += (a-b)*(a-b);
					count++;
				}
			}
			if (count < 100)
			{
				throw new Exception("Empty diffuse-GI reference");
			}
			return error / count;
		}
	}
}
'@
}
