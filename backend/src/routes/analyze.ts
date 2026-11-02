import { Router } from "express";
import { runAnalyzer } from "../services/analyzerRunner";

export const analyzeRouter = Router();

analyzeRouter.post("/", async (req, res) => {
  const code = req.body?.code;
  if (typeof code !== "string" || code.trim() === "") {
    return res.status(400).json({ error: "Please send some C code in the 'code' field." });
  }
  if (code.length > 200_000) {
    return res.status(413).json({ error: "Code is too long (limit: 200,000 characters)." });
  }
  try {
    res.json(await runAnalyzer(code));
  } catch (e) {
    res.status(500).json({ error: (e as Error).message });
  }
});
